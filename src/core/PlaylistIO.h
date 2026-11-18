#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace halo {

struct PlaylistEntry {
    QString path;          // absolute when read
    QString title;
    QString artist;
    qint64  durationMs = 0;
};

struct PlaylistFile {
    QString name;
    QVector<PlaylistEntry> entries;
};

// Reads and writes .m3u/.m3u8, .pls, .xspf and .zpl/.wpl (SMIL-style) playlists.
namespace PlaylistIO {
    QStringList supportedExtensions();
    QString fileFilter();
    std::optional<PlaylistFile> read(const QString &file, QString *error = nullptr);
    // When `relative` is true, paths are written relative to the playlist's folder
    // (needed for playlists copied onto a device).
    bool write(const QString &file, const PlaylistFile &pl, bool relative, QString *error = nullptr);
}

} // namespace halo
