#pragma once
#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

// Generates short sine-wave audio files with tags, using ffmpeg.
inline bool haveFfmpeg() { return !QStandardPaths::findExecutable("ffmpeg").isEmpty(); }

inline bool ffmpegHasEncoder(const QString &name)
{
    static QString list;
    if (list.isEmpty()) {
        QProcess p;
        p.start("ffmpeg", {"-hide_banner", "-encoders"});
        p.waitForFinished(30000);
        list = QString::fromUtf8(p.readAllStandardOutput());
    }
    return list.contains(' ' + name + ' ');
}

// `filter` is an optional extra audio filter, e.g. "volume=0.5".
inline bool makeTone(const QString &path, double seconds, const QMap<QString, QString> &tags,
                     double frequency = 440, const QString &filter = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QStringList args = {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                        QStringLiteral("sine=frequency=%1:duration=%2").arg(frequency).arg(seconds)};
    if (!filter.isEmpty()) args << "-af" << filter;
    for (auto it = tags.begin(); it != tags.end(); ++it) args << "-metadata" << it.key() + "=" + it.value();
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp3") args << "-c:a" << "libmp3lame" << "-b:a" << "128k";
    else if (ext == "m4a") args << "-c:a" << "aac" << "-b:a" << "128k";
    else if (ext == "ogg") {
        // Some ffmpeg builds (e.g. Homebrew) ship without libvorbis.
        if (ffmpegHasEncoder("libvorbis")) args << "-c:a" << "libvorbis";
        else args << "-c:a" << "vorbis" << "-strict" << "-2" << "-ac" << "2";
    }
    else if (ext == "opus") args << "-c:a" << "libopus";
    args << path;
    QProcess p;
    p.start("ffmpeg", args);
    return p.waitForFinished(30000) && p.exitCode() == 0;
}
