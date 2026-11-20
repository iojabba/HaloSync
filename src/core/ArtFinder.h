#pragma once
#include "MusicBrainz.h"
#include "Track.h"
#include <QHash>
#include <QObject>
#include <QTimer>

namespace halo {

class Library;

// Finds missing album covers in the background (SPEC §5, 2b): looks the album
// up on MusicBrainz, takes the front cover from the Cover Art Archive and saves
// it as folder.jpg. Only exact title/artist matches are used, MusicBrainz's
// one-request-per-second limit is respected, and each album is tried at most
// once a month.
class ArtFinder : public QObject {
    Q_OBJECT
public:
    explicit ArtFinder(Library *library, QObject *parent = nullptr);
    static bool enabled();                 // setting library/autoArt, default on

    void start(bool force = false);        // force: retry albums tried recently
    bool isRunning() const { return !m_queue.isEmpty() || m_busy; }

    struct Album { QString albumArtist, album; QStringList dirs; };
    // Albums without art whose folders hold only that album (safe to write folder.jpg).
    static QVector<Album> candidates(const TrackList &library);
    static QString normalize(const QString &s);   // for matching titles

signals:
    void progress(int done, int total);
    void finished(int found, int tried);

private:
    void next();
    void onSearch(const QVector<halo::MbRelease> &results);
    void onCover(const QImage &image);
    void onFailed(const QString &message);
    void finishAlbum(bool found);
    void loadTried();
    void saveTried();
    Library *m_lib;
    MusicBrainz m_mb;
    QTimer m_pace;
    QVector<Album> m_queue;
    Album m_current;
    QStringList m_releases;   // matching release ids still to try for a cover
    QHash<QString, qint64> m_tried;
    bool m_busy = false;
    int m_total = 0, m_done = 0, m_found = 0;
};

} // namespace halo
