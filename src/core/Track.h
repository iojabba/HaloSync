#pragma once
#include <QString>
#include <QDateTime>
#include <QMetaType>
#include <QVector>

namespace halo {

// Rating states from SPEC §2.2: Like (+1), Don't like (-1), Unrated (0).
enum Rating : int { Dislike = -1, Unrated = 0, Like = 1 };

struct Track {
    qint64  id = 0;
    QString path;
    QString title;
    QString artist;        // contributing / song artist
    QString albumArtist;
    QString album;
    QString genre;
    QString composer;
    int     year = 0;
    int     trackNo = 0;
    int     discNo = 0;
    qint64  durationMs = 0;
    int     bitrateKbps = 0;
    int     sampleRate = 0;
    qint64  sizeBytes = 0;
    qint64  mtime = 0;        // seconds since epoch
    qint64  dateAdded = 0;    // seconds since epoch
    qint64  lastPlayed = 0;   // seconds since epoch, 0 = never
    int     playCount = 0;
    int     rating = Unrated;
    double  rgTrackGain = 0;  // dB, 0 = none
    double  rgAlbumGain = 0;
    double  rgTrackPeak = 0;  // linear sample peak, 0 = none
    double  rgAlbumPeak = 0;
    bool    hasArt = false;
    QString format;           // lowercase file extension

    // Display helpers with the placeholders from SPEC §2.3.
    QString displayTitle() const;
    QString displayAlbumArtist() const;
    QString displayArtist() const;
    QString displayAlbum() const;
    QString displayGenre() const;
};

using TrackList = QVector<Track>;

inline QString Track::displayTitle() const {
    if (!title.isEmpty()) return title;
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.mid(slash + 1) : path;
}
inline QString Track::displayAlbumArtist() const {
    if (!albumArtist.isEmpty()) return albumArtist;
    if (!artist.isEmpty()) return artist;
    return QStringLiteral("Unknown Artist");
}
inline QString Track::displayArtist() const {
    return artist.isEmpty() ? displayAlbumArtist() : artist;
}
inline QString Track::displayAlbum() const {
    return album.isEmpty() ? QStringLiteral("Unknown Album") : album;
}
inline QString Track::displayGenre() const {
    return genre.isEmpty() ? QStringLiteral("Unknown Genre") : genre;
}

} // namespace halo

Q_DECLARE_METATYPE(halo::Track)
