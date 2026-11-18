#include "Library.h"
#include "Paths.h"
#include "AutoPlaylist.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

namespace halo {

Library::Library(Database *db, QObject *parent) : QObject(parent), m_db(db)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(1500);
    connect(&m_debounce, &QTimer::timeout, this, &Library::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_future, &QFutureWatcher<ScanResult>::finished, this, [this] {
        applyScanResult(m_future.result());
        m_scanning = false;
        if (m_rescanQueued) { m_rescanQueued = false; rescan(); }
    });
}

QString Library::addFolder(const QString &path, const QString &mediaType)
{
    QFileInfo fi(path);
    if (path.isEmpty() || !fi.isDir() || !fi.isReadable())
        return QStringLiteral("Either the path isn't valid or the folder can't be read.");
    const QString canonical = fi.canonicalFilePath();
    for (const MonitoredFolder &f : m_db->folders()) {
        if (canonical == f.path) return QStringLiteral("That folder is already monitored.");
        if (canonical.startsWith(f.path + '/')) return QStringLiteral("That folder is inside a monitored folder already.");
    }
    m_db->addFolder(canonical, mediaType);
    rescan();
    return {};
}

void Library::removeFolder(qint64 folderId)
{
    m_db->removeFolder(folderId, true);
    rescan();   // refresh watcher list
}

ScanResult Library::scan(const QStringList &roots, const QHash<QString, FileStamp> &known)
{
    ScanResult r;
    QSet<QString> seen;
    for (const QString &root : roots) {
        if (!QFileInfo(root).isDir()) {
            // Root is offline (e.g. unplugged external drive): keep its tracks.
            const QString prefix = root.endsWith('/') ? root : root + '/';
            for (auto it = known.cbegin(); it != known.cend(); ++it)
                if (it.key().startsWith(prefix)) seen.insert(it.key());
            continue;
        }
        r.directories << root;
        QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString p = it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isDir()) { r.directories << p; continue; }
            if (!TagIO::isAudioFile(p)) continue;
            const QString abs = fi.absoluteFilePath();
            seen.insert(abs);
            auto k = known.constFind(abs);
            if (k != known.cend() && k->mtime == fi.lastModified().toSecsSinceEpoch() && k->size == fi.size()) {
                ++r.unchanged;
                continue;
            }
            if (auto t = TagIO::read(abs)) r.changed.push_back(*t);
        }
    }
    for (auto it = known.cbegin(); it != known.cend(); ++it)
        if (!seen.contains(it.key())) r.removed.push_back(it->id);
    return r;
}

void Library::rescan()
{
    if (m_scanning) { m_rescanQueued = true; return; }
    m_scanning = true;
    emit scanStarted();
    QStringList roots;
    for (const MonitoredFolder &f : m_db->folders()) roots << f.path;
    const auto known = m_db->fileStamps();
    m_future.setFuture(QtConcurrent::run([roots, known] { return Library::scan(roots, known); }));
}

ScanResult Library::rescanBlocking()
{
    QStringList roots;
    for (const MonitoredFolder &f : m_db->folders()) roots << f.path;
    ScanResult r = scan(roots, m_db->fileStamps());
    applyScanResult(r);
    return r;
}

void Library::applyScanResult(const ScanResult &r)
{
    m_db->begin();
    for (const Track &t : r.changed) m_db->upsertTrack(t);
    if (!r.removed.isEmpty()) m_db->removeTracks(r.removed);
    m_db->commit();
    watchDirectories(r.directories);
    logLine("scan", QStringLiteral("%1 new/changed, %2 removed, %3 unchanged")
                        .arg(r.changed.size()).arg(r.removed.size()).arg(r.unchanged));
    emit scanFinished(int(r.changed.size()), int(r.removed.size()));
}

void Library::watchDirectories(const QStringList &dirs)
{
    // Native watchers are limited; watch the first few thousand directories.
    constexpr int kMax = 4000;
    const QStringList current = m_watcher.directories();
    if (!current.isEmpty()) m_watcher.removePaths(current);
    QStringList add = dirs.mid(0, kMax);
    if (!add.isEmpty()) m_watcher.addPaths(add);
}

bool Library::editTags(const QVector<qint64> &ids, const TagEdit &edit, QStringList *errors)
{
    if (edit.isEmpty()) return true;
    bool allOk = true;
    m_db->begin();
    for (qint64 id : ids) {
        auto t = m_db->track(id);
        if (!t) continue;
        QString err;
        if (!TagIO::write(t->path, edit, &err)) {
            allOk = false;
            if (errors) *errors << err;
            logLine("tags", err);
            continue;
        }
        if (auto fresh = TagIO::read(t->path)) m_db->upsertTrack(*fresh);
    }
    m_db->commit();
    return allOk;
}

int Library::merge(const QString &field, const QString &from, const QString &to, QStringList *errors)
{
    if (from == to || to.trimmed().isEmpty()) return 0;
    QVector<qint64> ids;
    for (const Track &t : m_db->allTracks()) {
        const QString v = field == "albumArtist" ? t.displayAlbumArtist()
                        : field == "artist"      ? t.displayArtist()
                        : field == "album"       ? t.displayAlbum()
                        : field == "genre"       ? t.displayGenre() : QString();
        if (v == from) ids << t.id;
    }
    TagEdit e;
    if (field == "albumArtist") e.albumArtist = to;
    else if (field == "artist") e.artist = to;
    else if (field == "album") e.album = to;
    else if (field == "genre") e.genre = to;
    else return 0;
    editTags(ids, e, errors);
    return int(ids.size());
}

bool Library::pasteAlbumArt(const QString &albumArtist, const QString &album, const QImage &image, QString *error)
{
    if (image.isNull()) { if (error) *error = QStringLiteral("The clipboard doesn't contain an image."); return false; }
    QSet<QString> dirs;
    for (const Track &t : m_db->allTracks())
        if (t.displayAlbumArtist() == albumArtist && t.displayAlbum() == album)
            dirs.insert(QFileInfo(t.path).absolutePath());
    if (dirs.isEmpty()) { if (error) *error = QStringLiteral("Album not found."); return false; }
    const QImage scaled = image.width() > 1000 ? image.scaledToWidth(1000, Qt::SmoothTransformation) : image;
    bool ok = true;
    for (const QString &d : dirs) {
        const QString target = d + "/folder.jpg";
        if (!scaled.save(target, "JPG", 90)) {
            ok = false;
            if (error) *error = QStringLiteral("Couldn't write %1").arg(target);
            continue;
        }
        m_db->setHasArt(d, true);
    }
    return ok;
}

void Library::removeFromCollection(const QVector<qint64> &ids, bool deleteFiles)
{
    if (deleteFiles) {
        for (qint64 id : ids)
            if (auto t = m_db->track(id))
                if (!QFile::remove(t->path)) logLine("library", "couldn't delete " + t->path);
    }
    m_db->removeTracks(ids);
}

TrackList Library::playlistTracks(qint64 playlistId, const TrackList &library) const
{
    auto pl = m_db->playlist(playlistId);
    if (!pl) return {};
    if (pl->kind == "auto")
        return AutoPlaylistSpec::fromJsonString(pl->rulesJson).evaluate(library, QDateTime::currentSecsSinceEpoch());
    QHash<qint64, int> byId;
    for (int i = 0; i < library.size(); ++i) byId.insert(library[i].id, i);
    TrackList out;
    for (qint64 id : m_db->playlistItems(playlistId)) {
        auto it = byId.constFind(id);
        if (it != byId.cend()) out.push_back(library[*it]);
    }
    return out;
}

} // namespace halo
