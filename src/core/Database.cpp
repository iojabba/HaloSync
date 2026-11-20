#include "Database.h"
#include "Paths.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QDateTime>
#include <QUuid>
#include <QVariant>
#include <QSet>

namespace halo {

namespace {
qint64 now() { return QDateTime::currentSecsSinceEpoch(); }

const char *kTrackColumns =
    "id, path, title, artist, album_artist, album, genre, composer, year, track_no, disc_no, "
    "duration_ms, bitrate, sample_rate, size, mtime, date_added, last_played, play_count, rating, "
    "rg_track_gain, rg_album_gain, has_art, format, rg_track_peak, rg_album_peak";

Track readTrack(const QSqlQuery &q)
{
    Track t;
    int c = 0;
    t.id          = q.value(c++).toLongLong();
    t.path        = q.value(c++).toString();
    t.title       = q.value(c++).toString();
    t.artist      = q.value(c++).toString();
    t.albumArtist = q.value(c++).toString();
    t.album       = q.value(c++).toString();
    t.genre       = q.value(c++).toString();
    t.composer    = q.value(c++).toString();
    t.year        = q.value(c++).toInt();
    t.trackNo     = q.value(c++).toInt();
    t.discNo      = q.value(c++).toInt();
    t.durationMs  = q.value(c++).toLongLong();
    t.bitrateKbps = q.value(c++).toInt();
    t.sampleRate  = q.value(c++).toInt();
    t.sizeBytes   = q.value(c++).toLongLong();
    t.mtime       = q.value(c++).toLongLong();
    t.dateAdded   = q.value(c++).toLongLong();
    t.lastPlayed  = q.value(c++).toLongLong();
    t.playCount   = q.value(c++).toInt();
    t.rating      = q.value(c++).toInt();
    t.rgTrackGain = q.value(c++).toDouble();
    t.rgAlbumGain = q.value(c++).toDouble();
    t.hasArt      = q.value(c++).toBool();
    t.format      = q.value(c++).toString();
    t.rgTrackPeak = q.value(c++).toDouble();
    t.rgAlbumPeak = q.value(c++).toDouble();
    return t;
}
} // namespace

Database::Database(QObject *parent) : QObject(parent)
{
    m_conn = QStringLiteral("halo-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

Database::~Database()
{
    if (m_db.isValid()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_conn);
}

bool Database::open(const QString &file)
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(file);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        logLine("db", "open failed: " + m_lastError);
        return false;
    }
    exec("PRAGMA foreign_keys = ON");
    exec("PRAGMA journal_mode = WAL");
    return createSchema();
}

bool Database::exec(const QString &sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        logLine("db", sql.left(80) + " -> " + m_lastError);
        return false;
    }
    return true;
}

bool Database::createSchema()
{
    const char *stmts[] = {
        "CREATE TABLE IF NOT EXISTS folders (id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, media_type TEXT NOT NULL DEFAULT 'music')",
        "CREATE TABLE IF NOT EXISTS tracks ("
        " id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, title TEXT, artist TEXT, album_artist TEXT, album TEXT,"
        " genre TEXT, composer TEXT, year INTEGER DEFAULT 0, track_no INTEGER DEFAULT 0, disc_no INTEGER DEFAULT 0,"
        " duration_ms INTEGER DEFAULT 0, bitrate INTEGER DEFAULT 0, sample_rate INTEGER DEFAULT 0, size INTEGER DEFAULT 0,"
        " mtime INTEGER DEFAULT 0, date_added INTEGER DEFAULT 0, last_played INTEGER DEFAULT 0, play_count INTEGER DEFAULT 0,"
        " rating INTEGER DEFAULT 0, rg_track_gain REAL DEFAULT 0, rg_album_gain REAL DEFAULT 0, has_art INTEGER DEFAULT 0, format TEXT)",
        "CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album_artist, album)",
        "CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL COLLATE NOCASE,"
        " kind TEXT NOT NULL DEFAULT 'static', rules TEXT)",
        "CREATE TABLE IF NOT EXISTS playlist_items (playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,"
        " position INTEGER NOT NULL, track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE)",
        "CREATE INDEX IF NOT EXISTS idx_pli ON playlist_items(playlist_id, position)",
        "CREATE TABLE IF NOT EXISTS pins (id INTEGER PRIMARY KEY, type TEXT, key TEXT, label TEXT, pinned_at INTEGER, UNIQUE(type, key))",
        "CREATE TABLE IF NOT EXISTS history (track_id INTEGER REFERENCES tracks(id) ON DELETE CASCADE, played_at INTEGER)",
        "CREATE TABLE IF NOT EXISTS devices (id INTEGER PRIMARY KEY, name TEXT, path TEXT, mode TEXT, options TEXT)",
        "CREATE TABLE IF NOT EXISTS sync_items (device_id INTEGER REFERENCES devices(id) ON DELETE CASCADE, kind TEXT, key TEXT,"
        " UNIQUE(device_id, kind, key))",
        "CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v TEXT)",
        "INSERT OR IGNORE INTO meta(k, v) VALUES('schema', '1')",
        // Schema 2 (0.2): radio favourites and cached content hashes for the duplicate finder.
        "CREATE TABLE IF NOT EXISTS radio_stations (id INTEGER PRIMARY KEY, name TEXT, url TEXT UNIQUE NOT NULL,"
        " homepage TEXT, favicon TEXT, tags TEXT, country TEXT, codec TEXT, bitrate INTEGER DEFAULT 0, added_at INTEGER)",
        "CREATE TABLE IF NOT EXISTS file_hashes (path TEXT PRIMARY KEY, size INTEGER, mtime INTEGER, hash TEXT)",
        "CREATE INDEX IF NOT EXISTS idx_history_track ON history(track_id, played_at)",
    };
    for (const char *s : stmts)
        if (!exec(QString::fromUtf8(s))) return false;
    // Columns added after schema 1.
    QSet<QString> cols;
    QSqlQuery info(m_db);
    info.exec("PRAGMA table_info(tracks)");
    while (info.next()) cols.insert(info.value(1).toString());
    for (const char *c : {"rg_track_peak", "rg_album_peak"})
        if (!cols.contains(QLatin1String(c)) &&
            !exec(QStringLiteral("ALTER TABLE tracks ADD COLUMN %1 REAL DEFAULT 0").arg(QLatin1String(c))))
            return false;
    exec("UPDATE meta SET v='2' WHERE k='schema'");
    return true;
}

void Database::begin() { if (m_txDepth++ == 0) m_db.transaction(); }
void Database::commit()
{
    if (m_txDepth > 0 && --m_txDepth == 0) {
        m_db.commit();
        if (m_pendingTracksChanged) { m_pendingTracksChanged = false; emit tracksChanged(); }
    }
}

// ------------------------------------------------------------------ folders
QVector<MonitoredFolder> Database::folders()
{
    QVector<MonitoredFolder> out;
    QSqlQuery q(m_db);
    q.exec("SELECT id, path, media_type FROM folders ORDER BY path");
    while (q.next()) out.push_back({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString()});
    return out;
}

qint64 Database::addFolder(const QString &path, const QString &mediaType)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO folders(path, media_type) VALUES(?, ?)");
    q.addBindValue(path);
    q.addBindValue(mediaType);
    q.exec();
    q.prepare("SELECT id FROM folders WHERE path = ?");
    q.addBindValue(path);
    q.exec();
    return q.next() ? q.value(0).toLongLong() : -1;
}

void Database::removeFolder(qint64 id, bool removeTracks)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT path FROM folders WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    if (!q.next()) return;
    QString path = q.value(0).toString();
    if (!path.endsWith('/')) path += '/';
    if (removeTracks) {
        // SPEC §2.2: items leave the collection; no files are deleted.
        QSqlQuery d(m_db);
        d.prepare("DELETE FROM tracks WHERE substr(path, 1, ?) = ?");
        d.addBindValue(path.size());
        d.addBindValue(path);
        d.exec();
    }
    QSqlQuery d(m_db);
    d.prepare("DELETE FROM folders WHERE id = ?");
    d.addBindValue(id);
    d.exec();
    emit tracksChanged();
}

// ------------------------------------------------------------------ tracks
TrackList Database::allTracks()
{
    TrackList out;
    QSqlQuery q(m_db);
    q.setForwardOnly(true);
    q.exec(QStringLiteral("SELECT %1 FROM tracks").arg(kTrackColumns));
    while (q.next()) out.push_back(readTrack(q));
    return out;
}

std::optional<Track> Database::track(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM tracks WHERE id = ?").arg(kTrackColumns));
    q.addBindValue(id);
    q.exec();
    if (q.next()) return readTrack(q);
    return std::nullopt;
}

std::optional<Track> Database::trackByPath(const QString &path)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM tracks WHERE path = ?").arg(kTrackColumns));
    q.addBindValue(path);
    q.exec();
    if (q.next()) return readTrack(q);
    return std::nullopt;
}

QHash<QString, FileStamp> Database::fileStamps()
{
    QHash<QString, FileStamp> out;
    QSqlQuery q(m_db);
    q.setForwardOnly(true);
    q.exec("SELECT path, mtime, size, id FROM tracks");
    while (q.next())
        out.insert(q.value(0).toString(), {q.value(1).toLongLong(), q.value(2).toLongLong(), q.value(3).toLongLong()});
    return out;
}

qint64 Database::upsertTrack(const Track &t)
{
    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO tracks(path, title, artist, album_artist, album, genre, composer, year, track_no, disc_no,"
        " duration_ms, bitrate, sample_rate, size, mtime, date_added, rg_track_gain, rg_album_gain, has_art, format,"
        " rg_track_peak, rg_album_peak)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(path) DO UPDATE SET title=excluded.title, artist=excluded.artist, album_artist=excluded.album_artist,"
        " album=excluded.album, genre=excluded.genre, composer=excluded.composer, year=excluded.year,"
        " track_no=excluded.track_no, disc_no=excluded.disc_no, duration_ms=excluded.duration_ms,"
        " bitrate=excluded.bitrate, sample_rate=excluded.sample_rate, size=excluded.size, mtime=excluded.mtime,"
        " rg_track_gain=excluded.rg_track_gain, rg_album_gain=excluded.rg_album_gain, has_art=excluded.has_art,"
        " format=excluded.format, rg_track_peak=excluded.rg_track_peak, rg_album_peak=excluded.rg_album_peak");
    q.addBindValue(t.path);
    q.addBindValue(t.title);
    q.addBindValue(t.artist);
    q.addBindValue(t.albumArtist);
    q.addBindValue(t.album);
    q.addBindValue(t.genre);
    q.addBindValue(t.composer);
    q.addBindValue(t.year);
    q.addBindValue(t.trackNo);
    q.addBindValue(t.discNo);
    q.addBindValue(t.durationMs);
    q.addBindValue(t.bitrateKbps);
    q.addBindValue(t.sampleRate);
    q.addBindValue(t.sizeBytes);
    q.addBindValue(t.mtime);
    q.addBindValue(t.dateAdded ? t.dateAdded : now());
    q.addBindValue(t.rgTrackGain);
    q.addBindValue(t.rgAlbumGain);
    q.addBindValue(t.hasArt ? 1 : 0);
    q.addBindValue(t.format);
    q.addBindValue(t.rgTrackPeak);
    q.addBindValue(t.rgAlbumPeak);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        logLine("db", "upsert failed for " + t.path + ": " + m_lastError);
        return -1;
    }
    if (m_txDepth > 0) m_pendingTracksChanged = true; else emit tracksChanged();
    QSqlQuery idq(m_db);
    idq.prepare("SELECT id FROM tracks WHERE path = ?");
    idq.addBindValue(t.path);
    idq.exec();
    return idq.next() ? idq.value(0).toLongLong() : -1;
}

void Database::upsertTracks(const TrackList &tracks)
{
    begin();
    for (const Track &t : tracks) upsertTrack(t);
    commit();
}

void Database::removeTrack(qint64 id) { removeTracks({id}); }

void Database::removeTracks(const QVector<qint64> &ids)
{
    begin();
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM tracks WHERE id = ?");
    for (qint64 id : ids) { q.addBindValue(id); q.exec(); }
    m_pendingTracksChanged = true;
    commit();
}

void Database::setRating(qint64 id, int rating)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE tracks SET rating = ? WHERE id = ?");
    q.addBindValue(qBound(-1, rating, 1));
    q.addBindValue(id);
    q.exec();
    emit tracksChanged();
}

void Database::mergeTrackStats(qint64 keep, const QVector<qint64> &from)
{
    if (from.isEmpty()) return;
    begin();
    for (qint64 id : from) {
        if (id == keep) continue;
        QSqlQuery q(m_db);
        q.prepare("UPDATE tracks SET play_count = play_count + (SELECT play_count FROM tracks WHERE id = :f),"
                  " last_played = MAX(last_played, (SELECT last_played FROM tracks WHERE id = :f)),"
                  " rating = CASE WHEN rating = 0 THEN (SELECT rating FROM tracks WHERE id = :f) ELSE rating END,"
                  " date_added = MIN(date_added, (SELECT date_added FROM tracks WHERE id = :f))"
                  " WHERE id = :k");
        q.bindValue(":f", id);
        q.bindValue(":k", keep);
        q.exec();
        for (const char *sql : {"UPDATE history SET track_id = :k WHERE track_id = :f",
                                "UPDATE playlist_items SET track_id = :k WHERE track_id = :f"}) {
            QSqlQuery u(m_db);
            u.prepare(QString::fromLatin1(sql));
            u.bindValue(":k", keep);
            u.bindValue(":f", id);
            u.exec();
        }
    }
    m_pendingTracksChanged = true;
    commit();
}

void Database::recordPlay(qint64 id)
{
    const qint64 ts = now();
    QSqlQuery q(m_db);
    q.prepare("UPDATE tracks SET play_count = play_count + 1, last_played = ? WHERE id = ?");
    q.addBindValue(ts);
    q.addBindValue(id);
    q.exec();
    q.prepare("INSERT INTO history(track_id, played_at) VALUES(?, ?)");
    q.addBindValue(id);
    q.addBindValue(ts);
    q.exec();
    emit tracksChanged();
    emit pinsChanged();   // recents on the Quickplay page
}

void Database::setHasArt(const QString &dirPath, bool hasArt)
{
    QString dir = dirPath.endsWith('/') ? dirPath : dirPath + '/';
    QSqlQuery q(m_db);
    q.prepare("UPDATE tracks SET has_art = ? WHERE substr(path, 1, ?) = ? AND instr(substr(path, ? + 1), '/') = 0");
    q.addBindValue(hasArt ? 1 : 0);
    q.addBindValue(dir.size());
    q.addBindValue(dir);
    q.addBindValue(dir.size());
    q.exec();
    emit tracksChanged();
}

// ------------------------------------------------------------------ playlists
QVector<Playlist> Database::playlists()
{
    QVector<Playlist> out;
    QSqlQuery q(m_db);
    q.exec("SELECT id, name, kind, rules FROM playlists ORDER BY name COLLATE NOCASE");
    while (q.next())
        out.push_back({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString()});
    return out;
}

std::optional<Playlist> Database::playlist(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id, name, kind, rules FROM playlists WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    if (q.next()) return Playlist{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString()};
    return std::nullopt;
}

std::optional<Playlist> Database::playlistByName(const QString &name)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM playlists WHERE name = ? COLLATE NOCASE");
    q.addBindValue(name);
    q.exec();
    if (q.next()) return playlist(q.value(0).toLongLong());
    return std::nullopt;
}

qint64 Database::createPlaylist(const QString &name, const QString &kind, const QString &rulesJson)
{
    if (name.trimmed().isEmpty() || playlistByName(name)) return -1;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO playlists(name, kind, rules) VALUES(?, ?, ?)");
    q.addBindValue(name.trimmed());
    q.addBindValue(kind);
    q.addBindValue(rulesJson);
    if (!q.exec()) return -1;
    const qint64 id = q.lastInsertId().toLongLong();
    emit playlistsChanged();
    return id;
}

bool Database::renamePlaylist(qint64 id, const QString &name)
{
    auto existing = playlistByName(name);
    if (name.trimmed().isEmpty() || (existing && existing->id != id)) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE playlists SET name = ? WHERE id = ?");
    q.addBindValue(name.trimmed());
    q.addBindValue(id);
    const bool ok = q.exec();
    emit playlistsChanged();
    return ok;
}

void Database::updatePlaylistRules(qint64 id, const QString &rulesJson)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE playlists SET rules = ? WHERE id = ?");
    q.addBindValue(rulesJson);
    q.addBindValue(id);
    q.exec();
    emit playlistsChanged();
}

void Database::deletePlaylist(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM playlists WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    q.prepare("DELETE FROM pins WHERE type = 'playlist' AND key = ?");
    q.addBindValue(QString::number(id));
    q.exec();
    emit playlistsChanged();
    emit pinsChanged();
}

QVector<qint64> Database::playlistItems(qint64 id)
{
    QVector<qint64> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT track_id FROM playlist_items WHERE playlist_id = ? ORDER BY position");
    q.addBindValue(id);
    q.exec();
    while (q.next()) out.push_back(q.value(0).toLongLong());
    return out;
}

void Database::setPlaylistItems(qint64 id, const QVector<qint64> &trackIds)
{
    begin();
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM playlist_items WHERE playlist_id = ?");
    q.addBindValue(id);
    q.exec();
    q.prepare("INSERT INTO playlist_items(playlist_id, position, track_id) VALUES(?, ?, ?)");
    for (int i = 0; i < trackIds.size(); ++i) {
        q.addBindValue(id);
        q.addBindValue(i);
        q.addBindValue(trackIds[i]);
        q.exec();
    }
    commit();
    emit playlistsChanged();
}

void Database::appendToPlaylist(qint64 id, const QVector<qint64> &trackIds)
{
    QVector<qint64> items = playlistItems(id);
    items += trackIds;
    setPlaylistItems(id, items);
}

// ------------------------------------------------------------------ pins / history
QVector<Pin> Database::pins()
{
    QVector<Pin> out;
    QSqlQuery q(m_db);
    q.exec("SELECT id, type, key, label, pinned_at FROM pins ORDER BY pinned_at DESC");
    while (q.next())
        out.push_back({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                       q.value(3).toString(), q.value(4).toLongLong()});
    return out;
}

void Database::addPin(const Pin &pin)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO pins(type, key, label, pinned_at) VALUES(?, ?, ?, ?)");
    q.addBindValue(pin.type);
    q.addBindValue(pin.key);
    q.addBindValue(pin.label);
    q.addBindValue(now());
    q.exec();
    emit pinsChanged();
}

void Database::removePin(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM pins WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    emit pinsChanged();
}

QVector<qint64> Database::recentTrackIds(int limit)
{
    QVector<qint64> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT track_id, MAX(played_at) AS t FROM history GROUP BY track_id ORDER BY t DESC LIMIT ?");
    q.addBindValue(limit);
    q.exec();
    while (q.next()) out.push_back(q.value(0).toLongLong());
    return out;
}

// ------------------------------------------------------------------ devices
QVector<DeviceProfile> Database::devices()
{
    QVector<DeviceProfile> out;
    QSqlQuery q(m_db);
    q.exec("SELECT id, name, path, mode, options FROM devices ORDER BY name");
    while (q.next()) {
        DeviceProfile d;
        d.id = q.value(0).toLongLong();
        d.name = q.value(1).toString();
        d.path = q.value(2).toString();
        d.mode = q.value(3).toString();
        d.options = QJsonDocument::fromJson(q.value(4).toByteArray()).object();
        out.push_back(d);
    }
    return out;
}

qint64 Database::saveDevice(const DeviceProfile &d)
{
    QSqlQuery q(m_db);
    const QByteArray opts = QJsonDocument(d.options).toJson(QJsonDocument::Compact);
    if (d.id > 0) {
        q.prepare("UPDATE devices SET name = ?, path = ?, mode = ?, options = ? WHERE id = ?");
        q.addBindValue(d.name);
        q.addBindValue(d.path);
        q.addBindValue(d.mode);
        q.addBindValue(QString::fromUtf8(opts));
        q.addBindValue(d.id);
        q.exec();
        emit devicesChanged();
        return d.id;
    }
    q.prepare("INSERT INTO devices(name, path, mode, options) VALUES(?, ?, ?, ?)");
    q.addBindValue(d.name);
    q.addBindValue(d.path);
    q.addBindValue(d.mode);
    q.addBindValue(QString::fromUtf8(opts));
    q.exec();
    emit devicesChanged();
    return q.lastInsertId().toLongLong();
}

void Database::deleteDevice(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM devices WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    emit devicesChanged();
}

QVector<SyncItem> Database::syncItems(qint64 deviceId)
{
    QVector<SyncItem> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT kind, key FROM sync_items WHERE device_id = ? ORDER BY kind, key");
    q.addBindValue(deviceId);
    q.exec();
    while (q.next()) out.push_back({q.value(0).toString(), q.value(1).toString()});
    return out;
}

void Database::addSyncItem(qint64 deviceId, const SyncItem &item)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO sync_items(device_id, kind, key) VALUES(?, ?, ?)");
    q.addBindValue(deviceId);
    q.addBindValue(item.kind);
    q.addBindValue(item.key);
    q.exec();
    emit devicesChanged();
}

void Database::removeSyncItem(qint64 deviceId, const SyncItem &item)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM sync_items WHERE device_id = ? AND kind = ? AND key = ?");
    q.addBindValue(deviceId);
    q.addBindValue(item.kind);
    q.addBindValue(item.key);
    q.exec();
    emit devicesChanged();
}

// ------------------------------------------------------------------ radio
QVector<RadioStation> Database::radioStations()
{
    QVector<RadioStation> out;
    QSqlQuery q(m_db);
    q.exec("SELECT id, name, url, homepage, favicon, tags, country, codec, bitrate FROM radio_stations ORDER BY name COLLATE NOCASE");
    while (q.next()) {
        RadioStation s;
        s.id = q.value(0).toLongLong();
        s.name = q.value(1).toString();
        s.url = q.value(2).toString();
        s.homepage = q.value(3).toString();
        s.favicon = q.value(4).toString();
        s.tags = q.value(5).toString();
        s.country = q.value(6).toString();
        s.codec = q.value(7).toString();
        s.bitrate = q.value(8).toInt();
        out << s;
    }
    return out;
}

qint64 Database::addRadioStation(const RadioStation &s)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO radio_stations(name, url, homepage, favicon, tags, country, codec, bitrate, added_at)"
              " VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(url) DO UPDATE SET name=excluded.name");
    for (const QVariant &v : {QVariant(s.name), QVariant(s.url), QVariant(s.homepage), QVariant(s.favicon), QVariant(s.tags),
                              QVariant(s.country), QVariant(s.codec), QVariant(s.bitrate), QVariant(now())})
        q.addBindValue(v);
    if (!q.exec()) { m_lastError = q.lastError().text(); return 0; }
    QSqlQuery id(m_db);
    id.prepare("SELECT id FROM radio_stations WHERE url = ?");
    id.addBindValue(s.url);
    id.exec();
    emit radioChanged();
    return id.next() ? id.value(0).toLongLong() : 0;
}

void Database::removeRadioStation(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM radio_stations WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    emit radioChanged();
}

// ------------------------------------------------------------------ history
QVector<QPair<qint64, qint64>> Database::history(qint64 since)
{
    QVector<QPair<qint64, qint64>> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT track_id, played_at FROM history WHERE played_at >= ? ORDER BY played_at DESC");
    q.addBindValue(since);
    q.exec();
    while (q.next()) out.push_back({q.value(0).toLongLong(), q.value(1).toLongLong()});
    return out;
}

QString Database::cachedHash(const QString &path, qint64 size, qint64 mtime)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT hash FROM file_hashes WHERE path = ? AND size = ? AND mtime = ?");
    q.addBindValue(path);
    q.addBindValue(size);
    q.addBindValue(mtime);
    q.exec();
    return q.next() ? q.value(0).toString() : QString();
}

void Database::storeHash(const QString &path, qint64 size, qint64 mtime, const QString &hash)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO file_hashes(path, size, mtime, hash) VALUES(?,?,?,?)");
    q.addBindValue(path);
    q.addBindValue(size);
    q.addBindValue(mtime);
    q.addBindValue(hash);
    q.exec();
}

} // namespace halo
