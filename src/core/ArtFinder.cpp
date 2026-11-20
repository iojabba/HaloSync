#include "ArtFinder.h"
#include "Library.h"
#include "Paths.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace halo {

namespace {
QString albumKey(const QString &artist, const QString &album) { return artist.toLower() + '\x1f' + album.toLower(); }
QString triedFile() { return Paths::dataDir() + "/art-lookups.json"; }
constexpr qint64 RetryAfter = 30LL * 24 * 3600;
}

ArtFinder::ArtFinder(Library *library, QObject *parent) : QObject(parent), m_lib(library)
{
    m_pace.setSingleShot(true);
    m_pace.setInterval(1100);   // MusicBrainz allows one request per second
    connect(&m_pace, &QTimer::timeout, this, &ArtFinder::next);
    connect(&m_mb, &MusicBrainz::searchFinished, this, &ArtFinder::onSearch);
    connect(&m_mb, &MusicBrainz::coverFinished, this, &ArtFinder::onCover);
    connect(&m_mb, &MusicBrainz::failed, this, &ArtFinder::onFailed);
}

bool ArtFinder::enabled() { return Paths::settings()->value("library/autoArt", true).toBool(); }

QString ArtFinder::normalize(const QString &s)
{
    QString t = s.toLower();
    // Drop bracketed edition notes: "Album (Deluxe Edition)" matches "Album".
    static const QRegularExpression brackets(QStringLiteral("\\s*[\\(\\[].*?[\\)\\]]"));
    static const QRegularExpression nonword(QStringLiteral("[^\\p{L}\\p{N}]+"));
    t.remove(brackets);
    t.replace(QStringLiteral("&"), QStringLiteral(" and "));
    t.replace(nonword, QStringLiteral(" "));
    t = t.simplified();
    if (t.startsWith(QLatin1String("the "))) t = t.mid(4);
    return t;
}

QVector<ArtFinder::Album> ArtFinder::candidates(const TrackList &library)
{
    QHash<QString, QSet<QString>> albumsInDir;      // dir -> album keys
    QHash<QString, Album> albums;
    for (const Track &t : library) {
        if (t.album.isEmpty() || t.format == QLatin1String("stream")) continue;
        const QString dir = QFileInfo(t.path).absolutePath();
        const QString key = albumKey(t.displayAlbumArtist(), t.album);
        albumsInDir[dir].insert(key);
        if (t.hasArt) { albums[key].albumArtist = QStringLiteral("\x01"); continue; }   // has art somewhere: skip
        Album &a = albums[key];
        if (a.albumArtist == QLatin1String("\x01")) continue;
        a.albumArtist = t.displayAlbumArtist();
        a.album = t.album;
        if (!a.dirs.contains(dir)) a.dirs << dir;
    }
    QVector<Album> out;
    for (const Album &a : albums) {
        if (a.albumArtist == QLatin1String("\x01") || a.dirs.isEmpty()) continue;
        bool safe = true;
        for (const QString &d : a.dirs) if (albumsInDir.value(d).size() != 1) safe = false;
        if (safe) out << a;
    }
    return out;
}

void ArtFinder::loadTried()
{
    m_tried.clear();
    QFile f(triedFile());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) m_tried.insert(it.key(), qint64(it.value().toDouble()));
}

void ArtFinder::saveTried()
{
    QJsonObject o;
    for (auto it = m_tried.begin(); it != m_tried.end(); ++it) o.insert(it.key(), double(it.value()));
    QFile f(triedFile());
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void ArtFinder::start(bool force)
{
    if (isRunning()) return;
    loadTried();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_queue.clear();
    for (const Album &a : candidates(m_lib->db()->allTracks())) {
        const qint64 when = m_tried.value(albumKey(a.albumArtist, a.album), 0);
        if (force || now - when > RetryAfter) m_queue << a;
    }
    m_total = int(m_queue.size());
    m_done = m_found = 0;
    if (m_queue.isEmpty()) { emit finished(0, 0); return; }
    logLine("art", QStringLiteral("looking up covers for %1 albums").arg(m_total));
    next();
}

void ArtFinder::next()
{
    if (!m_releases.isEmpty()) {           // try the next matching release's cover
        m_busy = true;
        m_mb.fetchCover(m_releases.takeFirst());
        return;
    }
    if (m_queue.isEmpty()) {
        m_busy = false;
        saveTried();
        logLine("art", QStringLiteral("found %1 of %2 covers").arg(m_found).arg(m_total));
        emit finished(m_found, m_total);
        return;
    }
    m_busy = true;
    m_current = m_queue.takeFirst();
    m_mb.searchReleases(m_current.albumArtist, m_current.album);
}

void ArtFinder::onSearch(const QVector<MbRelease> &results)
{
    if (!m_busy) return;
    const QString album = normalize(m_current.album);
    const QString artist = normalize(m_current.albumArtist);
    m_releases.clear();
    for (const MbRelease &r : results) {
        if (normalize(r.title) != album) continue;
        const QString ra = normalize(r.artist);
        const bool various = artist == QLatin1String("various artists") || artist == QLatin1String("various");
        if (ra == artist || (various && ra == QLatin1String("various artists")) || ra.startsWith(artist + ' '))
            m_releases << r.id;
        if (m_releases.size() >= 3) break;
    }
    if (m_releases.isEmpty()) { finishAlbum(false); return; }
    m_pace.start();
}

void ArtFinder::onCover(const QImage &image)
{
    if (!m_busy) return;
    QString err;
    const bool ok = m_lib->pasteAlbumArt(m_current.albumArtist, m_current.album, image, &err);
    if (!ok) logLine("art", err);
    m_releases.clear();
    finishAlbum(ok);
}

void ArtFinder::onFailed(const QString &message)
{
    if (!m_busy) return;
    if (!m_releases.isEmpty()) { m_pace.start(); return; }   // no cover on this release: try the next
    if (!message.startsWith(QLatin1String("No cover"))) logLine("art", m_current.album + ": " + message);
    finishAlbum(false);
}

void ArtFinder::finishAlbum(bool found)
{
    m_tried.insert(albumKey(m_current.albumArtist, m_current.album), QDateTime::currentSecsSinceEpoch());
    if (found) ++m_found;
    emit progress(++m_done, m_total);
    if (m_done % 10 == 0) saveTried();
    m_pace.start();
}

} // namespace halo
