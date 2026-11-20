#include "TagIO.h"
#include "Paths.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/audioproperties.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/flacfile.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>

namespace halo {

namespace {
#ifdef _WIN32
TagLib::FileName toFileName(const QString &p) { return reinterpret_cast<const wchar_t *>(p.utf16()); }
#endif

QString qs(const TagLib::String &s) { return QString::fromStdString(s.to8Bit(true)).trimmed(); }
TagLib::String ts(const QString &s) { return TagLib::String(s.toStdString(), TagLib::String::UTF8); }

QString firstProp(const TagLib::PropertyMap &m, const char *key)
{
    auto it = m.find(key);
    if (it == m.end() || it->second.isEmpty()) return {};
    return qs(it->second.front());
}

int leadingInt(const QString &s)   // "3/12" -> 3, "2009-05-01" -> 2009
{
    int i = 0;
    while (i < s.size() && s[i].isDigit()) ++i;
    return s.left(i).toInt();
}

double parseGain(const QString &s)   // "-6.20 dB"
{
    QString t = s;
    t.remove(QStringLiteral("dB"), Qt::CaseInsensitive);
    bool ok = false;
    const double v = t.trimmed().toDouble(&ok);
    return ok ? v : 0.0;
}

#ifdef _WIN32
#define HALO_FN(p) toFileName(p)
#else
#define HALO_FN(p) QFile::encodeName(p).constData()
#endif
} // namespace

QStringList TagIO::audioExtensions()
{
    return {"mp3", "m4a", "m4b", "mp4", "aac", "flac", "ogg", "oga", "opus", "wma", "wav", "aif", "aiff", "ape", "wv"};
}

bool TagIO::isAudioFile(const QString &path)
{
    return audioExtensions().contains(QFileInfo(path).suffix().toLower());
}

std::optional<Track> TagIO::read(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.isFile()) return std::nullopt;

    Track t;
    t.path = fi.absoluteFilePath();
    t.sizeBytes = fi.size();
    t.mtime = fi.lastModified().toSecsSinceEpoch();
    t.format = fi.suffix().toLower();

    TagLib::FileRef f(HALO_FN(t.path), true, TagLib::AudioProperties::Average);
    if (f.isNull()) {
        // Unreadable tags: keep the file in the collection with file-name title.
        logLine("tags", "no tag reader for " + t.path);
        return t;
    }
    if (TagLib::Tag *tag = f.tag()) {
        t.title = qs(tag->title());
        t.artist = qs(tag->artist());
        t.album = qs(tag->album());
        t.genre = qs(tag->genre());
        t.year = int(tag->year());
        t.trackNo = int(tag->track());
    }
    const TagLib::PropertyMap props = f.file()->properties();
    t.albumArtist = firstProp(props, "ALBUMARTIST");
    t.composer = firstProp(props, "COMPOSER");
    t.discNo = leadingInt(firstProp(props, "DISCNUMBER"));
    if (!t.year) t.year = leadingInt(firstProp(props, "DATE"));
    t.rgTrackGain = parseGain(firstProp(props, "REPLAYGAIN_TRACK_GAIN"));
    t.rgAlbumGain = parseGain(firstProp(props, "REPLAYGAIN_ALBUM_GAIN"));
    t.rgTrackPeak = parseGain(firstProp(props, "REPLAYGAIN_TRACK_PEAK"));
    t.rgAlbumPeak = parseGain(firstProp(props, "REPLAYGAIN_ALBUM_PEAK"));

    if (TagLib::AudioProperties *ap = f.audioProperties()) {
        t.durationMs = ap->lengthInMilliseconds();
        t.bitrateKbps = ap->bitrate();
        t.sampleRate = ap->sampleRate();
    }
    t.hasArt = hasEmbeddedArt(t.path) || hasFolderArt(fi.absolutePath());
    return t;
}

bool TagIO::write(const QString &path, const TagEdit &e, QString *error)
{
    TagLib::FileRef f(HALO_FN(path), false);
    if (f.isNull() || !f.file()) {
        if (error) *error = QStringLiteral("Can't open %1 for tag writing").arg(path);
        return false;
    }
    if (f.file()->readOnly()) {
        if (error) *error = QStringLiteral("%1 is read-only").arg(path);
        return false;
    }
    TagLib::PropertyMap props = f.file()->properties();
    auto setText = [&](const char *key, const std::optional<QString> &v) {
        if (!v) return;
        if (v->trimmed().isEmpty()) props.erase(key);
        else props.replace(key, TagLib::StringList(ts(v->trimmed())));
    };
    auto setNum = [&](const char *key, const std::optional<int> &v) {
        if (!v) return;
        if (*v <= 0) props.erase(key);
        else props.replace(key, TagLib::StringList(ts(QString::number(*v))));
    };
    setText("TITLE", e.title);
    setText("ARTIST", e.artist);
    setText("ALBUMARTIST", e.albumArtist);
    setText("ALBUM", e.album);
    setText("GENRE", e.genre);
    setText("COMPOSER", e.composer);
    setNum("DATE", e.year);
    setNum("TRACKNUMBER", e.trackNo);
    setNum("DISCNUMBER", e.discNo);
    f.file()->setProperties(props);
    if (!f.save()) {
        if (error) *error = QStringLiteral("Saving tags to %1 failed").arg(path);
        return false;
    }
    return true;
}

bool TagIO::writeReplayGain(const QString &path, double trackGain, double trackPeak, double albumGain,
                            double albumPeak, QString *error)
{
    TagLib::FileRef f(HALO_FN(path), false);
    if (f.isNull() || !f.file() || f.file()->readOnly()) {
        if (error) *error = QStringLiteral("Can't write tags to %1").arg(path);
        return false;
    }
    TagLib::PropertyMap props = f.file()->properties();
    auto put = [&](const char *key, const QString &v) { props.replace(key, TagLib::StringList(ts(v))); };
    put("REPLAYGAIN_TRACK_GAIN", QStringLiteral("%1 dB").arg(trackGain, 0, 'f', 2));
    put("REPLAYGAIN_TRACK_PEAK", QString::number(trackPeak, 'f', 6));
    if (albumPeak > 0) {
        put("REPLAYGAIN_ALBUM_GAIN", QStringLiteral("%1 dB").arg(albumGain, 0, 'f', 2));
        put("REPLAYGAIN_ALBUM_PEAK", QString::number(albumPeak, 'f', 6));
    }
    f.file()->setProperties(props);
    if (!f.save()) {
        if (error) *error = QStringLiteral("Saving tags to %1 failed").arg(path);
        return false;
    }
    return true;
}

QString TagIO::embeddedLyrics(const QString &path)
{
    TagLib::FileRef f(HALO_FN(path), false);
    if (f.isNull() || !f.file()) return {};
    const TagLib::PropertyMap props = f.file()->properties();
    auto it = props.find("LYRICS");
    if (it == props.end() || it->second.isEmpty()) return {};
    return QString::fromStdString(it->second.front().to8Bit(true));
}

bool TagIO::hasEmbeddedArt(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp3") {
        TagLib::MPEG::File f(HALO_FN(path), false);
        if (f.isValid() && f.ID3v2Tag()) return !f.ID3v2Tag()->frameListMap()["APIC"].isEmpty();
    } else if (ext == "flac") {
        TagLib::FLAC::File f(HALO_FN(path), false);
        if (f.isValid()) return !f.pictureList().isEmpty();
    } else if (ext == "m4a" || ext == "m4b" || ext == "mp4") {
        TagLib::MP4::File f(HALO_FN(path), false);
        if (f.isValid() && f.tag()) return f.tag()->contains("covr");
    }
    return false;
}

QString TagIO::folderArtPath(const QString &dirPath)
{
    static const QStringList names = {"folder.jpg", "cover.jpg", "front.jpg", "albumart.jpg", "folder.png", "cover.png"};
    QDir d(dirPath);
    const QStringList entries = d.entryList(QDir::Files);
    for (const QString &n : names)
        for (const QString &e : entries)
            if (e.compare(n, Qt::CaseInsensitive) == 0) return d.filePath(e);
    return {};
}

bool TagIO::hasFolderArt(const QString &dirPath) { return !folderArtPath(dirPath).isEmpty(); }

void TagIO::apply(Track &t, const TagEdit &e)
{
    if (e.title) t.title = e.title->trimmed();
    if (e.artist) t.artist = e.artist->trimmed();
    if (e.albumArtist) t.albumArtist = e.albumArtist->trimmed();
    if (e.album) t.album = e.album->trimmed();
    if (e.genre) t.genre = e.genre->trimmed();
    if (e.composer) t.composer = e.composer->trimmed();
    if (e.year) t.year = qMax(0, *e.year);
    if (e.trackNo) t.trackNo = qMax(0, *e.trackNo);
    if (e.discNo) t.discNo = qMax(0, *e.discNo);
}

} // namespace halo
