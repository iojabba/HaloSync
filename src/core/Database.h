#pragma once
#include "Track.h"
#include <QObject>
#include <QSqlDatabase>
#include <QHash>
#include <QJsonObject>
#include <optional>

namespace halo {

struct MonitoredFolder {
    qint64  id = 0;
    QString path;
    QString mediaType = QStringLiteral("music");
};

struct Playlist {
    qint64  id = 0;
    QString name;
    QString kind = QStringLiteral("static");   // "static" or "auto"
    QString rulesJson;                         // AutoPlaylist rules, kind == auto
};

struct Pin {
    qint64  id = 0;
    QString type;   // artist | album | genre | playlist
    QString key;    // artist name, "albumArtist\x1falbum", genre, or playlist id
    QString label;
    qint64  pinnedAt = 0;
};

struct DeviceProfile {
    qint64      id = 0;
    QString     name;
    QString     path;                          // mount point / folder
    QString     mode = QStringLiteral("manual"); // all | chosen | manual
    QJsonObject options;                       // formats, codec, bitrate, threshold, ...
};

struct SyncItem {
    QString kind;   // artist | album | genre | playlist | track
    QString key;
};

struct FileStamp { qint64 mtime = 0; qint64 size = 0; qint64 id = 0; };

class Database : public QObject {
    Q_OBJECT
public:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    bool open(const QString &file);
    QString lastError() const { return m_lastError; }

    // Monitored folders (SPEC §2.2)
    QVector<MonitoredFolder> folders();
    qint64 addFolder(const QString &path, const QString &mediaType = QStringLiteral("music"));
    void removeFolder(qint64 id, bool removeTracks = true);

    // Tracks
    TrackList allTracks();
    std::optional<Track> track(qint64 id);
    std::optional<Track> trackByPath(const QString &path);
    QHash<QString, FileStamp> fileStamps();
    qint64 upsertTrack(const Track &t);     // keeps rating / play stats / dateAdded
    void   upsertTracks(const TrackList &tracks);
    void   removeTrack(qint64 id);
    void   removeTracks(const QVector<qint64> &ids);
    void   setRating(qint64 id, int rating);
    void   recordPlay(qint64 id);
    void   setHasArt(const QString &dirPath, bool hasArt);

    // Playlists (SPEC §2.5)
    QVector<Playlist> playlists();
    std::optional<Playlist> playlist(qint64 id);
    std::optional<Playlist> playlistByName(const QString &name);
    qint64 createPlaylist(const QString &name, const QString &kind = QStringLiteral("static"),
                          const QString &rulesJson = {});  // -1 if name taken
    bool   renamePlaylist(qint64 id, const QString &name);
    void   updatePlaylistRules(qint64 id, const QString &rulesJson);
    void   deletePlaylist(qint64 id);
    QVector<qint64> playlistItems(qint64 id);
    void   setPlaylistItems(qint64 id, const QVector<qint64> &trackIds);
    void   appendToPlaylist(qint64 id, const QVector<qint64> &trackIds);

    // Quickplay (SPEC §2.6)
    QVector<Pin> pins();
    void addPin(const Pin &pin);
    void removePin(qint64 id);
    QVector<qint64> recentTrackIds(int limit);

    // Devices / sync (SPEC §2.12)
    QVector<DeviceProfile> devices();
    qint64 saveDevice(const DeviceProfile &d);
    void   deleteDevice(qint64 id);
    QVector<SyncItem> syncItems(qint64 deviceId);
    void   addSyncItem(qint64 deviceId, const SyncItem &item);
    void   removeSyncItem(qint64 deviceId, const SyncItem &item);

    // Transactions for bulk updates
    void begin();
    void commit();

signals:
    void tracksChanged();
    void playlistsChanged();
    void pinsChanged();
    void devicesChanged();

private:
    bool exec(const QString &sql);
    bool createSchema();
    QSqlDatabase m_db;
    QString m_conn;
    QString m_lastError;
    int m_txDepth = 0;
    bool m_pendingTracksChanged = false;
};

} // namespace halo
