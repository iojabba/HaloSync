#include "PlaylistIO.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QRegularExpression>

namespace halo {

namespace {

QString resolve(const QString &baseDir, QString p)
{
    p = p.trimmed();
    if (p.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) p = QUrl(p).toLocalFile();
    p.replace('\\', '/');
    if (p.isEmpty()) return {};
    // Windows drive paths ("C:/...") are absolute even on other systems.
    static const QRegularExpression drive(QStringLiteral("^[A-Za-z]:/"));
    if (QDir::isAbsolutePath(p) || drive.match(p).hasMatch()) return QDir::cleanPath(p);
    return QDir::cleanPath(baseDir + '/' + p);
}

QString outPath(const QString &baseDir, const QString &path, bool relative)
{
    return relative ? QDir(baseDir).relativeFilePath(path) : QDir::toNativeSeparators(path);
}

PlaylistFile readM3u(const QString &text, const QString &base, const QString &fallbackName)
{
    PlaylistFile pl{fallbackName, {}};
    PlaylistEntry pending;
    for (QString line : text.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1String("#EXTINF:"))) {
            // #EXTINF:<seconds>,<artist> - <title>
            const QString body = line.mid(8);
            const int comma = body.indexOf(',');
            pending.durationMs = qMax(0LL, body.left(comma).toLongLong() * 1000);
            const QString disp = comma >= 0 ? body.mid(comma + 1) : QString();
            const int dash = disp.indexOf(QLatin1String(" - "));
            if (dash > 0) { pending.artist = disp.left(dash); pending.title = disp.mid(dash + 3); }
            else pending.title = disp;
            continue;
        }
        if (line.startsWith(QLatin1String("#PLAYLIST:"))) { pl.name = line.mid(10).trimmed(); continue; }
        if (line.startsWith('#')) continue;
        pending.path = resolve(base, line);
        pl.entries.push_back(pending);
        pending = {};
    }
    return pl;
}

PlaylistFile readPls(const QString &text, const QString &base, const QString &fallbackName)
{
    PlaylistFile pl{fallbackName, {}};
    QMap<int, PlaylistEntry> byIndex;
    static const QRegularExpression re(QStringLiteral("^(File|Title|Length)(\\d+)=(.*)$"), QRegularExpression::CaseInsensitiveOption);
    for (const QString &raw : text.split('\n')) {
        const auto m = re.match(raw.trimmed());
        if (!m.hasMatch()) continue;
        PlaylistEntry &e = byIndex[m.captured(2).toInt()];
        const QString key = m.captured(1).toLower();
        if (key == "file") e.path = resolve(base, m.captured(3));
        else if (key == "title") e.title = m.captured(3);
        else e.durationMs = qMax(0LL, m.captured(3).toLongLong() * 1000);
    }
    for (const PlaylistEntry &e : byIndex) if (!e.path.isEmpty()) pl.entries.push_back(e);
    return pl;
}

PlaylistFile readXspf(QXmlStreamReader &x, const QString &base, const QString &fallbackName)
{
    PlaylistFile pl{fallbackName, {}};
    PlaylistEntry cur;
    bool inTrack = false;
    while (!x.atEnd()) {
        x.readNext();
        if (x.isStartElement()) {
            const auto n = x.name();
            if (n == QLatin1String("track")) { inTrack = true; cur = {}; }
            else if (n == QLatin1String("location") && inTrack) cur.path = resolve(base, x.readElementText());
            else if (n == QLatin1String("title")) { const QString t = x.readElementText(); if (inTrack) cur.title = t; else pl.name = t; }
            else if (n == QLatin1String("creator") && inTrack) cur.artist = x.readElementText();
            else if (n == QLatin1String("duration") && inTrack) cur.durationMs = x.readElementText().toLongLong();
        } else if (x.isEndElement() && x.name() == QLatin1String("track")) {
            inTrack = false;
            if (!cur.path.isEmpty()) pl.entries.push_back(cur);
        }
    }
    return pl;
}

PlaylistFile readSmil(QXmlStreamReader &x, const QString &base, const QString &fallbackName)
{
    PlaylistFile pl{fallbackName, {}};
    while (!x.atEnd()) {
        x.readNext();
        if (!x.isStartElement()) continue;
        if (x.name() == QLatin1String("title")) {
            pl.name = x.readElementText();
        } else if (x.name() == QLatin1String("media")) {
            PlaylistEntry e;
            e.path = resolve(base, x.attributes().value("src").toString());
            e.title = x.attributes().value("trackTitle").toString();
            e.artist = x.attributes().value("trackArtist").toString();
            e.durationMs = x.attributes().value("duration").toLongLong();
            if (!e.path.isEmpty()) pl.entries.push_back(e);
        }
    }
    return pl;
}

} // namespace

QStringList PlaylistIO::supportedExtensions() { return {"m3u", "m3u8", "pls", "xspf", "zpl", "wpl"}; }

QString PlaylistIO::fileFilter()
{
    return QStringLiteral("Playlists (*.m3u *.m3u8 *.pls *.xspf *.zpl *.wpl);;M3U (*.m3u *.m3u8);;PLS (*.pls);;XSPF (*.xspf);;Zune playlist (*.zpl)");
}

std::optional<PlaylistFile> PlaylistIO::read(const QString &file, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Can't open %1").arg(file);
        return std::nullopt;
    }
    const QFileInfo fi(file);
    const QString base = fi.absolutePath();
    const QString ext = fi.suffix().toLower();
    const QString name = fi.completeBaseName();
    const QByteArray data = f.readAll();

    if (ext == "xspf" || ext == "zpl" || ext == "wpl") {
        QXmlStreamReader x(data);
        PlaylistFile pl = ext == "xspf" ? readXspf(x, base, name) : readSmil(x, base, name);
        if (x.hasError()) {
            if (error) *error = QStringLiteral("%1: %2").arg(file, x.errorString());
            return std::nullopt;
        }
        if (pl.name.isEmpty()) pl.name = name;
        return pl;
    }
    // .m3u is often Latin-1 on Windows; .m3u8 is UTF-8. Try UTF-8 first.
    QString text = QString::fromUtf8(data);
    if (ext == "m3u" && text.contains(QChar::ReplacementCharacter)) text = QString::fromLatin1(data);
    text.remove(QChar(0xFEFF));
    text.replace(QLatin1String("\r"), QString());
    if (ext == "pls") return readPls(text, base, name);
    if (ext == "m3u" || ext == "m3u8") return readM3u(text, base, name);
    if (error) *error = QStringLiteral("Unsupported playlist format: .%1").arg(ext);
    return std::nullopt;
}

bool PlaylistIO::write(const QString &file, const PlaylistFile &pl, bool relative, QString *error)
{
    const QFileInfo fi(file);
    const QString base = fi.absolutePath();
    const QString ext = fi.suffix().toLower();
    QDir().mkpath(base);
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Can't write %1").arg(file);
        return false;
    }

    if (ext == "xspf") {
        QXmlStreamWriter w(&f);
        w.setAutoFormatting(true);
        w.writeStartDocument();
        w.writeStartElement("playlist");
        w.writeAttribute("version", "1");
        w.writeDefaultNamespace("http://xspf.org/ns/0/");
        w.writeTextElement("title", pl.name);
        w.writeStartElement("trackList");
        for (const PlaylistEntry &e : pl.entries) {
            w.writeStartElement("track");
            const QString loc = relative ? QDir(base).relativeFilePath(e.path)
                                         : QUrl::fromLocalFile(e.path).toString(QUrl::FullyEncoded);
            w.writeTextElement("location", loc);
            if (!e.title.isEmpty()) w.writeTextElement("title", e.title);
            if (!e.artist.isEmpty()) w.writeTextElement("creator", e.artist);
            if (e.durationMs > 0) w.writeTextElement("duration", QString::number(e.durationMs));
            w.writeEndElement();
        }
        w.writeEndElement();
        w.writeEndElement();
        w.writeEndDocument();
    } else if (ext == "zpl" || ext == "wpl") {
        QXmlStreamWriter w(&f);
        w.setAutoFormatting(true);
        w.writeProcessingInstruction(ext == "zpl" ? "zpl" : "wpl", "version=\"2.0\"");
        w.writeStartElement("smil");
        w.writeStartElement("head");
        w.writeTextElement("title", pl.name);
        w.writeEndElement();
        w.writeStartElement("body");
        w.writeStartElement("seq");
        for (const PlaylistEntry &e : pl.entries) {
            w.writeStartElement("media");
            w.writeAttribute("src", outPath(base, e.path, relative));
            if (!e.title.isEmpty()) w.writeAttribute("trackTitle", e.title);
            if (!e.artist.isEmpty()) w.writeAttribute("trackArtist", e.artist);
            if (e.durationMs > 0) w.writeAttribute("duration", QString::number(e.durationMs));
            w.writeEndElement();
        }
        w.writeEndElement();
        w.writeEndElement();
        w.writeEndElement();
    } else if (ext == "pls") {
        QTextStream out(&f);
        out << "[playlist]\n";
        int i = 1;
        for (const PlaylistEntry &e : pl.entries) {
            out << "File" << i << '=' << outPath(base, e.path, relative) << '\n';
            if (!e.title.isEmpty()) out << "Title" << i << '=' << e.title << '\n';
            out << "Length" << i << '=' << (e.durationMs > 0 ? e.durationMs / 1000 : -1) << '\n';
            ++i;
        }
        out << "NumberOfEntries=" << pl.entries.size() << "\nVersion=2\n";
    } else if (ext == "m3u" || ext == "m3u8") {
        QTextStream out(&f);
        out << "#EXTM3U\n";
        if (!pl.name.isEmpty()) out << "#PLAYLIST:" << pl.name << '\n';
        for (const PlaylistEntry &e : pl.entries) {
            const QString disp = e.artist.isEmpty() ? e.title : e.artist + " - " + e.title;
            out << "#EXTINF:" << (e.durationMs > 0 ? e.durationMs / 1000 : -1) << ',' << disp << '\n';
            out << outPath(base, e.path, relative) << '\n';
        }
    } else {
        f.cancelWriting();
        if (error) *error = QStringLiteral("Unsupported playlist format: .%1").arg(ext);
        return false;
    }
    if (!f.commit()) {
        if (error) *error = QStringLiteral("Can't write %1").arg(file);
        return false;
    }
    return true;
}

} // namespace halo
