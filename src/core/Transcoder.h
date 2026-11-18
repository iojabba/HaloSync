#pragma once
#include <QString>
#include <QStringList>
#include <atomic>

namespace halo {

struct TranscodeTarget {
    QString codec = QStringLiteral("mp3");   // mp3 | aac | opus | flac | wma
    int     bitrateKbps = 192;                // ignored for flac
    QString extension() const;
    static QStringList codecs();
};

// Wraps the ffmpeg command-line tool. Looks for ffmpeg next to the executable
// first, then on PATH, or uses the path saved in settings ("sync/ffmpeg").
namespace Transcoder {
    QString ffmpegPath();
    bool available();
    // Cache file name for (source, target). Unique per source mtime/size.
    QString cachePath(const QString &source, const TranscodeTarget &target);
    // Blocking. Returns true on success. `cancel` may be null.
    bool transcode(const QString &source, const QString &dest, const TranscodeTarget &target,
                   QString *error = nullptr, const std::atomic_bool *cancel = nullptr);
    // Keeps the cache under maxBytes by deleting the oldest files.
    void pruneCache(qint64 maxBytes);
    qint64 cacheSize();
    void clearCache();
}

} // namespace halo
