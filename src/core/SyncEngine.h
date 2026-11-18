#pragma once
#include "Database.h"
#include "Transcoder.h"
#include <QJsonObject>
#include <atomic>
#include <functional>

namespace halo {

// Per-device options (stored as JSON in DeviceProfile::options). SPEC §2.12.
struct SyncOptions {
    QStringList     supportedFormats = {"mp3", "m4a", "aac", "wma"}; // device plays these as-is
    TranscodeTarget target;                                          // used for everything else
    int             thresholdKbps = 0;     // >0: re-encode supported files above this bit rate
    bool            skipDisliked = true;   // "Don't sync songs rated Don't like"
    bool            writePlaylists = true; // copy playlists as relative .m3u
    QString         musicDir = QStringLiteral("Music");
    QString         playlistDir = QStringLiteral("Playlists");

    QJsonObject toJson() const;
    static SyncOptions fromJson(const QJsonObject &o);
};

struct SyncAction {
    enum Kind { Copy, Transcode, Delete };
    Kind    kind = Copy;
    Track   source;          // empty for Delete
    QString destRel;         // path relative to the device root
    qint64  bytes = 0;       // estimated bytes written (or freed for Delete)
};

struct SyncPlan {
    QVector<SyncAction> actions;
    int     upToDate = 0;
    int     skippedDisliked = 0;
    qint64  bytesToWrite = 0;
    qint64  bytesToFree = 0;
    qint64  freeBytes = 0;
    bool    overfilled() const { return bytesToWrite - bytesToFree > freeBytes; }
    int     count(SyncAction::Kind k) const;
};

struct StorageBreakdown {
    qint64 total = 0, free = 0, music = 0, video = 0, pictures = 0, podcasts = 0, other = 0;
    bool   valid = false;
};

struct SyncReport {
    int copied = 0, transcoded = 0, deleted = 0, failed = 0;
    bool cancelled = false;
    QStringList errors;
};

class SyncEngine {
public:
    using PlaylistResolver = std::function<TrackList(qint64 playlistId)>;
    using Progress = std::function<void(int done, int total, const QString &what)>;

    // Which tracks belong on the device. mode "all" = whole library;
    // "chosen"/"manual" = the device's sync items (artist/album/genre/playlist/track).
    static TrackList selectTracks(const DeviceProfile &device, const QVector<SyncItem> &items,
                                  const TrackList &library, const PlaylistResolver &playlists);

    static QString destPathFor(const Track &t, const SyncOptions &o);
    static bool needsTranscode(const Track &t, const SyncOptions &o);

    // deleteUnlisted: remove files this app put on the device earlier that are
    // no longer selected (automatic modes). Manual mode never deletes.
    static SyncPlan plan(const QString &deviceRoot, const TrackList &tracks, const SyncOptions &o,
                         bool deleteUnlisted);

    static SyncReport execute(const QString &deviceRoot, const SyncPlan &plan, const SyncOptions &o,
                              const QVector<QPair<QString, TrackList>> &playlists,
                              const Progress &progress = {}, const std::atomic_bool *cancel = nullptr);

    static StorageBreakdown storage(const QString &deviceRoot);
    static QString manifestFile(const QString &deviceRoot);

private:
    struct ManifestEntry { QString source; qint64 srcMtime = 0; qint64 srcSize = 0; QString codec; };
    static QHash<QString, ManifestEntry> loadManifest(const QString &deviceRoot);
    static bool saveManifest(const QString &deviceRoot, const QHash<QString, ManifestEntry> &m);
};

} // namespace halo
