#pragma once
#include "Database.h"
#include "TagIO.h"
#include <QObject>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QImage>
#include <QTimer>

namespace halo {

struct ScanResult {
    TrackList        changed;   // new or modified files, tags read
    QVector<qint64>  removed;   // track ids whose files vanished
    int              unchanged = 0;
    QStringList      directories; // all directories seen (for the watcher)
};

class Library : public QObject {
    Q_OBJECT
public:
    explicit Library(Database *db, QObject *parent = nullptr);

    Database *db() const { return m_db; }

    // Monitored folders (SPEC §2.2). Returns an error message or empty.
    QString addFolder(const QString &path, const QString &mediaType = QStringLiteral("music"));
    void removeFolder(qint64 folderId);

    void rescan();                 // asynchronous, incremental
    bool isScanning() const { return m_scanning; }
    ScanResult rescanBlocking();   // synchronous (CLI and tests)

    // Pure scan step: walks the roots, reads tags of new/changed files.
    static ScanResult scan(const QStringList &roots, const QHash<QString, FileStamp> &known);

    // Editing (SPEC §2.4). Writes tags into files, then refreshes the DB.
    bool editTags(const QVector<qint64> &ids, const TagEdit &edit, QStringList *errors = nullptr);
    // Merge: every track whose `field` equals `from` gets `to` (field: albumArtist|artist|album|genre).
    int  merge(const QString &field, const QString &from, const QString &to, QStringList *errors = nullptr);
    // Saves `image` as folder.jpg next to the album's files.
    bool pasteAlbumArt(const QString &albumArtist, const QString &album, const QImage &image, QString *error = nullptr);
    // Removes tracks from the collection; optionally deletes files (setting).
    void removeFromCollection(const QVector<qint64> &ids, bool deleteFiles, bool toTrash = false);

    void applyScanResult(const ScanResult &r);

    // Songs of a playlist: stored order for static lists, rule result for autoplaylists.
    TrackList playlistTracks(qint64 playlistId, const TrackList &library) const;
    TrackList playlistTracks(qint64 playlistId) const { return playlistTracks(playlistId, m_db->allTracks()); }

signals:
    void scanStarted();
    void scanFinished(int changed, int removed);

private:
    void watchDirectories(const QStringList &dirs);
    Database *m_db;
    QFileSystemWatcher m_watcher;
    QFutureWatcher<ScanResult> m_future;
    QTimer m_debounce;
    bool m_scanning = false;
    bool m_rescanQueued = false;
};

} // namespace halo
