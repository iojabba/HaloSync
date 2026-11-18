#include "SyncEngine.h"
#include "PlaylistIO.h"
#include "TagIO.h"
#include "Paths.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QRegularExpression>

namespace halo {

// ------------------------------------------------------------------ options
QJsonObject SyncOptions::toJson() const
{
    QJsonObject o;
    o["formats"] = QJsonArray::fromStringList(supportedFormats);
    o["codec"] = target.codec;
    o["bitrate"] = target.bitrateKbps;
    o["threshold"] = thresholdKbps;
    o["skipDisliked"] = skipDisliked;
    o["writePlaylists"] = writePlaylists;
    o["musicDir"] = musicDir;
    o["playlistDir"] = playlistDir;
    return o;
}

SyncOptions SyncOptions::fromJson(const QJsonObject &o)
{
    SyncOptions s;
    if (o.contains("formats")) {
        s.supportedFormats.clear();
        for (const auto &v : o["formats"].toArray()) s.supportedFormats << v.toString().toLower();
    }
    s.target.codec = o["codec"].toString(s.target.codec);
    s.target.bitrateKbps = o["bitrate"].toInt(s.target.bitrateKbps);
    s.thresholdKbps = o["threshold"].toInt(0);
    s.skipDisliked = o["skipDisliked"].toBool(true);
    s.writePlaylists = o["writePlaylists"].toBool(true);
    s.musicDir = o["musicDir"].toString(s.musicDir);
    s.playlistDir = o["playlistDir"].toString(s.playlistDir);
    return s;
}

int SyncPlan::count(SyncAction::Kind k) const
{
    int n = 0;
    for (const SyncAction &a : actions) if (a.kind == k) ++n;
    return n;
}

// ------------------------------------------------------------------ selection
TrackList SyncEngine::selectTracks(const DeviceProfile &device, const QVector<SyncItem> &items,
                                   const TrackList &library, const PlaylistResolver &playlists)
{
    if (device.mode == "all") return library;
    QSet<QString> artists, albums, genres, paths;
    TrackList out;
    QSet<qint64> added;
    for (const SyncItem &i : items) {
        if (i.kind == "artist") artists.insert(i.key);
        else if (i.kind == "album") albums.insert(i.key);
        else if (i.kind == "genre") genres.insert(i.key);
        else if (i.kind == "track") paths.insert(i.key);
        else if (i.kind == "playlist" && playlists) {
            for (const Track &t : playlists(i.key.toLongLong()))
                if (!added.contains(t.id)) { added.insert(t.id); out.push_back(t); }
        }
    }
    for (const Track &t : library) {
        if (added.contains(t.id)) continue;
        if (artists.contains(t.displayAlbumArtist()) ||
            albums.contains(t.displayAlbumArtist() + QChar(0x1f) + t.displayAlbum()) ||
            genres.contains(t.displayGenre()) || paths.contains(t.path)) {
            added.insert(t.id);
            out.push_back(t);
        }
    }
    return out;
}

// ------------------------------------------------------------------ paths
namespace {
QString sanitize(QString s, int maxLen = 80)
{
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    s.replace(bad, QStringLiteral("_"));
    s = s.trimmed();
    while (s.endsWith('.') || s.endsWith(' ')) s.chop(1);   // FAT/Windows rules
    if (s.isEmpty()) s = QStringLiteral("_");
    static const QStringList reserved = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "LPT1", "LPT2"};
    if (reserved.contains(s.toUpper())) s += '_';
    return s.left(maxLen);
}
} // namespace

bool SyncEngine::needsTranscode(const Track &t, const SyncOptions &o)
{
    if (!o.supportedFormats.contains(t.format.toLower())) return true;
    return o.thresholdKbps > 0 && t.bitrateKbps > o.thresholdKbps;
}

QString SyncEngine::destPathFor(const Track &t, const SyncOptions &o)
{
    const QString ext = needsTranscode(t, o) ? o.target.extension() : t.format.toLower();
    QString name;
    if (t.trackNo > 0) {
        name = t.discNo > 1 ? QStringLiteral("%1-%2 ").arg(t.discNo).arg(t.trackNo, 2, 10, QChar('0'))
                            : QStringLiteral("%1 ").arg(t.trackNo, 2, 10, QChar('0'));
    }
    QString title = t.title.isEmpty() ? QFileInfo(t.path).completeBaseName() : t.title;
    name += title;
    return o.musicDir + '/' + sanitize(t.displayAlbumArtist()) + '/' + sanitize(t.displayAlbum()) + '/' +
           sanitize(name, 120) + '.' + ext;
}

QString SyncEngine::manifestFile(const QString &deviceRoot)
{
    return deviceRoot + "/.halosync/manifest.json";
}

QHash<QString, SyncEngine::ManifestEntry> SyncEngine::loadManifest(const QString &deviceRoot)
{
    QHash<QString, ManifestEntry> m;
    QFile f(manifestFile(deviceRoot));
    if (!f.open(QIODevice::ReadOnly)) return m;
    const QJsonObject files = QJsonDocument::fromJson(f.readAll()).object()["files"].toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        m.insert(it.key(), {e["src"].toString(), qint64(e["mtime"].toDouble()), qint64(e["size"].toDouble()),
                            e["codec"].toString()});
    }
    return m;
}

bool SyncEngine::saveManifest(const QString &deviceRoot, const QHash<QString, ManifestEntry> &m)
{
    QDir().mkpath(deviceRoot + "/.halosync");
    QJsonObject files;
    for (auto it = m.begin(); it != m.end(); ++it) {
        QJsonObject e;
        e["src"] = it->source;
        e["mtime"] = double(it->srcMtime);
        e["size"] = double(it->srcSize);
        e["codec"] = it->codec;
        files.insert(it.key(), e);
    }
    QJsonObject root;
    root["version"] = 1;
    root["files"] = files;
    QSaveFile f(manifestFile(deviceRoot));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

// ------------------------------------------------------------------ planning
SyncPlan SyncEngine::plan(const QString &deviceRoot, const TrackList &tracks, const SyncOptions &o,
                          bool deleteUnlisted)
{
    SyncPlan p;
    const auto manifest = loadManifest(deviceRoot);
    QHash<QString, QString> sourceToDest;   // keep existing placement for a source
    for (auto it = manifest.begin(); it != manifest.end(); ++it) sourceToDest.insert(it->source, it.key());

    QSet<QString> wanted;
    for (const Track &t : tracks) {
        if (o.skipDisliked && t.rating == halo::Dislike) { ++p.skippedDisliked; continue; }
        const bool tx = needsTranscode(t, o);
        const QString codec = tx ? o.target.codec : QStringLiteral("copy");
        QString dest = destPathFor(t, o);
        // Reuse the existing location if this source is already on the device
        // with the same extension (avoids churn after small tag edits).
        if (auto prev = sourceToDest.constFind(t.path); prev != sourceToDest.cend() &&
                QFileInfo(*prev).suffix() == QFileInfo(dest).suffix())
            dest = *prev;
        // Two different songs mapping to one name: add a counter.
        for (int n = 2; wanted.contains(dest.toLower()); ++n) {
            const QFileInfo fi(dest);
            dest = fi.path() + '/' + fi.completeBaseName() + QStringLiteral(" (%1).").arg(n) + fi.suffix();
        }
        wanted.insert(dest.toLower());

        auto m = manifest.constFind(dest);
        const bool onDevice = QFileInfo::exists(deviceRoot + '/' + dest);
        if (m != manifest.cend() && onDevice && m->source == t.path && m->srcMtime == t.mtime &&
            m->srcSize == t.sizeBytes && m->codec == codec) {
            ++p.upToDate;
            continue;
        }
        SyncAction a;
        a.kind = tx ? SyncAction::Transcode : SyncAction::Copy;
        a.source = t;
        a.destRel = dest;
        if (tx && o.target.codec != "flac" && t.durationMs > 0)
            a.bytes = t.durationMs * o.target.bitrateKbps / 8;   // ms * kbit/s / 8 = bytes
        else
            a.bytes = t.sizeBytes;
        p.bytesToWrite += a.bytes;
        if (onDevice) p.bytesToFree += QFileInfo(deviceRoot + '/' + dest).size();
        p.actions.push_back(a);
    }

    if (deleteUnlisted) {
        for (auto it = manifest.begin(); it != manifest.end(); ++it) {
            if (wanted.contains(it.key().toLower())) continue;
            SyncAction a;
            a.kind = SyncAction::Delete;
            a.destRel = it.key();
            a.bytes = QFileInfo(deviceRoot + '/' + it.key()).size();
            p.bytesToFree += a.bytes;
            p.actions.push_back(a);
        }
    }
    const QStorageInfo si(deviceRoot);
    p.freeBytes = si.isValid() ? si.bytesAvailable() : 0;
    return p;
}

// ------------------------------------------------------------------ execution
namespace {
void removeEmptyParents(const QString &root, QString dir)
{
    const QString cleanRoot = QDir::cleanPath(root);
    while (QDir::cleanPath(dir) != cleanRoot && dir.startsWith(cleanRoot)) {
        if (!QDir(dir).isEmpty() || !QDir().rmdir(dir)) break;
        dir = QFileInfo(dir).absolutePath();
    }
}

bool copyAtomically(const QString &src, const QString &dest, QString *error)
{
    QDir().mkpath(QFileInfo(dest).absolutePath());
    const QString tmp = dest + ".halopart";
    QFile::remove(tmp);
    if (!QFile::copy(src, tmp)) {
        if (error) *error = QStringLiteral("Copy failed: %1").arg(dest);
        return false;
    }
    QFile::remove(dest);
    if (!QFile::rename(tmp, dest)) {
        QFile::remove(tmp);
        if (error) *error = QStringLiteral("Couldn't finish writing %1").arg(dest);
        return false;
    }
    return true;
}
} // namespace

SyncReport SyncEngine::execute(const QString &deviceRoot, const SyncPlan &plan, const SyncOptions &o,
                               const QVector<QPair<QString, TrackList>> &playlists,
                               const Progress &progress, const std::atomic_bool *cancel)
{
    SyncReport r;
    if (plan.overfilled()) {
        r.errors << QStringLiteral("Not enough space on the device. Uncheck items until it's no longer overfilled.");
        return r;
    }
    auto manifest = loadManifest(deviceRoot);
    const int total = int(plan.actions.size());
    int done = 0;

    // Deletes first, so space is free before writing.
    QVector<const SyncAction *> ordered;
    for (const SyncAction &a : plan.actions) if (a.kind == SyncAction::Delete) ordered << &a;
    for (const SyncAction &a : plan.actions) if (a.kind != SyncAction::Delete) ordered << &a;

    for (const SyncAction *a : ordered) {
        if (cancel && cancel->load()) { r.cancelled = true; break; }
        const QString dest = deviceRoot + '/' + a->destRel;
        if (progress) progress(done, total, a->kind == SyncAction::Delete ? QStringLiteral("Removing %1").arg(a->destRel)
                                                                          : a->source.displayTitle());
        QString err;
        bool ok = true;
        switch (a->kind) {
        case SyncAction::Delete:
            if (QFile::exists(dest) && !QFile::remove(dest)) { ok = false; err = QStringLiteral("Couldn't remove %1").arg(dest); }
            else { manifest.remove(a->destRel); removeEmptyParents(deviceRoot, QFileInfo(dest).absolutePath()); ++r.deleted; }
            break;
        case SyncAction::Copy:
            ok = copyAtomically(a->source.path, dest, &err);
            if (ok) { manifest.insert(a->destRel, {a->source.path, a->source.mtime, a->source.sizeBytes, "copy"}); ++r.copied; }
            break;
        case SyncAction::Transcode: {
            const QString cached = Transcoder::cachePath(a->source.path, o.target);
            if (!QFile::exists(cached)) ok = Transcoder::transcode(a->source.path, cached, o.target, &err, cancel);
            if (ok) ok = copyAtomically(cached, dest, &err);
            if (ok) { manifest.insert(a->destRel, {a->source.path, a->source.mtime, a->source.sizeBytes, o.target.codec}); ++r.transcoded; }
            break;
        }
        }
        if (!ok) {
            if (cancel && cancel->load()) { r.cancelled = true; break; }
            ++r.failed;
            r.errors << err;
            logLine("sync", err);
        }
        if (++done % 25 == 0) saveManifest(deviceRoot, manifest);
    }
    saveManifest(deviceRoot, manifest);

    // Playlists as relative .m3u files (only songs actually on the device).
    if (o.writePlaylists && !r.cancelled) {
        QHash<QString, QString> sourceToDest;
        for (auto it = manifest.begin(); it != manifest.end(); ++it) sourceToDest.insert(it->source, it.key());
        for (const auto &pl : playlists) {
            PlaylistFile f{pl.first, {}};
            for (const Track &t : pl.second) {
                auto d = sourceToDest.constFind(t.path);
                if (d == sourceToDest.cend()) continue;
                f.entries.push_back({deviceRoot + '/' + *d, t.displayTitle(), t.displayArtist(), t.durationMs});
            }
            QString err;
            const QString file = deviceRoot + '/' + o.playlistDir + '/' + sanitize(pl.first) + ".m3u";
            if (!PlaylistIO::write(file, f, true, &err)) r.errors << err;
        }
    }

    auto s = Paths::settings();
    Transcoder::pruneCache(qint64(s->value("sync/cacheMaxMB", 2048).toInt()) * 1024 * 1024);
    if (progress) progress(total, total, QString());
    logLine("sync", QStringLiteral("%1: copied %2, converted %3, removed %4, failed %5%6")
        .arg(deviceRoot).arg(r.copied).arg(r.transcoded).arg(r.deleted).arg(r.failed)
        .arg(r.cancelled ? ", cancelled" : ""));
    return r;
}

// ------------------------------------------------------------------ storage
StorageBreakdown SyncEngine::storage(const QString &deviceRoot)
{
    StorageBreakdown b;
    const QStorageInfo si(deviceRoot);
    if (!si.isValid() || !QFileInfo(deviceRoot).isDir()) return b;
    b.valid = true;
    b.total = si.bytesTotal();
    b.free = si.bytesAvailable();
    static const QStringList video = {"mp4", "m4v", "wmv", "avi", "mkv", "mov", "webm"};
    static const QStringList pics = {"jpg", "jpeg", "png", "gif", "bmp", "heic", "webp"};
    const QStringList audio = TagIO::audioExtensions();
    qint64 counted = 0;
    QDirIterator it(deviceRoot, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const qint64 sz = fi.size();
        const QString ext = fi.suffix().toLower();
        const QString lowerPath = fi.absoluteFilePath().toLower();
        counted += sz;
        if (audio.contains(ext)) {
            if (lowerPath.contains("/podcast")) b.podcasts += sz; else b.music += sz;
        } else if (video.contains(ext)) b.video += sz;
        else if (pics.contains(ext)) b.pictures += sz;
        else b.other += sz;
    }
    // Space used by things outside the folder (whole-disk targets) counts as other.
    const qint64 used = b.total - b.free;
    if (used > counted) b.other += used - counted;
    return b;
}

} // namespace halo
