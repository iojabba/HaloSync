#include "Transcoder.h"
#include "Paths.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>

namespace halo {

QString TranscodeTarget::extension() const
{
    if (codec == "aac") return QStringLiteral("m4a");
    if (codec == "opus") return QStringLiteral("opus");
    if (codec == "flac") return QStringLiteral("flac");
    if (codec == "wma") return QStringLiteral("wma");
    return QStringLiteral("mp3");
}

QStringList TranscodeTarget::codecs() { return {"mp3", "aac", "opus", "wma", "flac"}; }

QString Transcoder::ffmpegPath()
{
    auto s = Paths::settings();
    const QString configured = s->value("sync/ffmpeg").toString();
    if (!configured.isEmpty() && QFileInfo(configured).isExecutable()) return configured;
#ifdef _WIN32
    const QString local = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
#else
    const QString local = QCoreApplication::applicationDirPath() + "/ffmpeg";
#endif
    if (QFileInfo(local).isExecutable()) return local;
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

bool Transcoder::available() { return !ffmpegPath().isEmpty(); }

QString Transcoder::cachePath(const QString &source, const TranscodeTarget &target)
{
    const QFileInfo fi(source);
    const QByteArray key = QStringLiteral("%1|%2|%3|%4|%5")
        .arg(fi.absoluteFilePath()).arg(fi.lastModified().toSecsSinceEpoch()).arg(fi.size())
        .arg(target.codec).arg(target.bitrateKbps).toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    return Paths::transcodeCacheDir() + '/' + hash + '.' + target.extension();
}

bool Transcoder::transcode(const QString &source, const QString &dest, const TranscodeTarget &target,
                           QString *error, const std::atomic_bool *cancel)
{
    const QString ff = ffmpegPath();
    if (ff.isEmpty()) {
        if (error) *error = QStringLiteral("ffmpeg wasn't found. Install it or set its path in Settings.");
        return false;
    }
    QDir().mkpath(QFileInfo(dest).absolutePath());
    const QString tmp = dest + ".part." + target.extension();
    QStringList args = {"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i", source,
                        "-map", "0:a:0", "-map_metadata", "0", "-vn"};
    const QString br = QString::number(target.bitrateKbps) + "k";
    if (target.codec == "mp3")       args << "-c:a" << "libmp3lame" << "-b:a" << br << "-id3v2_version" << "3";
    else if (target.codec == "aac")  args << "-c:a" << "aac" << "-b:a" << br;
    else if (target.codec == "opus") args << "-c:a" << "libopus" << "-b:a" << br;
    else if (target.codec == "wma")  args << "-c:a" << "wmav2" << "-b:a" << br;
    else if (target.codec == "flac") args << "-c:a" << "flac";
    args << tmp;

    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(ff, args);
    if (!p.waitForStarted(10000)) {
        if (error) *error = QStringLiteral("Couldn't start ffmpeg.");
        return false;
    }
    while (!p.waitForFinished(250)) {
        if (cancel && cancel->load()) {
            p.kill();
            p.waitForFinished();
            QFile::remove(tmp);
            if (error) *error = QStringLiteral("Cancelled.");
            return false;
        }
        if (p.state() == QProcess::NotRunning) break;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        if (error) *error = QStringLiteral("ffmpeg failed on %1: %2")
            .arg(QFileInfo(source).fileName(), QString::fromUtf8(p.readAll()).trimmed().left(300));
        QFile::remove(tmp);
        return false;
    }
    QFile::remove(dest);
    if (!QFile::rename(tmp, dest)) {
        if (error) *error = QStringLiteral("Couldn't move the converted file into place.");
        QFile::remove(tmp);
        return false;
    }
    return true;
}

qint64 Transcoder::cacheSize()
{
    qint64 total = 0;
    QDirIterator it(Paths::transcodeCacheDir(), QDir::Files);
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}

void Transcoder::pruneCache(qint64 maxBytes)
{
    QVector<QFileInfo> files;
    qint64 total = 0;
    QDirIterator it(Paths::transcodeCacheDir(), QDir::Files);
    while (it.hasNext()) { it.next(); files << it.fileInfo(); total += it.fileInfo().size(); }
    if (total <= maxBytes) return;
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified(); });
    for (const QFileInfo &f : files) {
        if (total <= maxBytes) break;
        total -= f.size();
        QFile::remove(f.absoluteFilePath());
    }
}

void Transcoder::clearCache() { pruneCache(0); }

} // namespace halo
