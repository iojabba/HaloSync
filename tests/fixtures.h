#pragma once
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

// Generates short sine-wave audio files with tags, using ffmpeg.
inline bool haveFfmpeg() { return !QStandardPaths::findExecutable("ffmpeg").isEmpty(); }

inline bool makeTone(const QString &path, double seconds, const QMap<QString, QString> &tags)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QStringList args = {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                        QStringLiteral("sine=frequency=440:duration=%1").arg(seconds)};
    for (auto it = tags.begin(); it != tags.end(); ++it) args << "-metadata" << it.key() + "=" + it.value();
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp3") args << "-c:a" << "libmp3lame" << "-b:a" << "128k";
    else if (ext == "m4a") args << "-c:a" << "aac" << "-b:a" << "128k";
    else if (ext == "ogg") args << "-c:a" << "libvorbis";
    else if (ext == "opus") args << "-c:a" << "libopus";
    args << path;
    QProcess p;
    p.start("ffmpeg", args);
    return p.waitForFinished(30000) && p.exitCode() == 0;
}
