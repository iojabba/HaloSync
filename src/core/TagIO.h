#pragma once
#include "Track.h"
#include <QStringList>
#include <optional>

namespace halo {

// Fields a tag edit may change; unset optionals are left alone (multi-edit).
struct TagEdit {
    std::optional<QString> title, artist, albumArtist, album, genre, composer;
    std::optional<int> year, trackNo, discNo;
    bool isEmpty() const {
        return !title && !artist && !albumArtist && !album && !genre && !composer && !year && !trackNo && !discNo;
    }
};

namespace TagIO {
    QStringList audioExtensions();               // lowercase, no dot
    bool isAudioFile(const QString &path);
    std::optional<Track> read(const QString &path);
    bool write(const QString &path, const TagEdit &edit, QString *error = nullptr);
    bool hasEmbeddedArt(const QString &path);
    bool hasFolderArt(const QString &dirPath);
    QString folderArtPath(const QString &dirPath); // existing cover image or empty
    void apply(Track &t, const TagEdit &edit);   // mirror an edit into a Track
    // ReplayGain 2.0 tags (REPLAYGAIN_TRACK_GAIN/PEAK, and album values when albumPeak > 0).
    bool writeReplayGain(const QString &path, double trackGain, double trackPeak,
                         double albumGain, double albumPeak, QString *error = nullptr);
    QString embeddedLyrics(const QString &path);  // LYRICS / USLT, empty if none
}

} // namespace halo
