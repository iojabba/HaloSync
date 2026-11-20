#include "Lyrics.h"
#include "Paths.h"
#include "TagIO.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>
#include <algorithm>

namespace halo {

int Lyrics::lineAt(qint64 ms) const
{
    if (!synced) return -1;
    int found = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines[i].ms > ms) break;
        found = i;
    }
    return found;
}

LyricsService::LyricsService(QObject *parent) : QObject(parent) { qRegisterMetaType<halo::Lyrics>(); }

bool LyricsService::enabled() { return Paths::settings()->value("lyrics/enabled", true).toBool(); }
bool LyricsService::onlineEnabled() { return Paths::settings()->value("lyrics/online", true).toBool(); }

Lyrics LyricsService::parse(const QString &text)
{
    Lyrics out;
    // [mm:ss.xx] stamps; a line may carry several. Metadata tags like [ar:...] are skipped.
    static const QRegularExpression stamp(QStringLiteral("\\[(\\d{1,3}):(\\d{1,2})(?:[.:](\\d{1,3}))?\\]"));
    static const QRegularExpression meta(QStringLiteral("^\\[[a-zA-Z]+:.*\\]$"));
    const QStringList raw = text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
    QVector<LyricLine> timed, plain;
    for (const QString &l : raw) {
        QString line = l.trimmed();
        QVector<qint64> times;
        auto it = stamp.globalMatch(line);
        int end = 0;
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedStart() != end) break;      // stamps only count at the start
            end = int(m.capturedEnd());
            QString frac = m.captured(3);
            const int fracMs = frac.isEmpty() ? 0 : int(frac.leftJustified(3, '0').left(3).toInt());
            times << (m.captured(1).toLongLong() * 60 + m.captured(2).toLongLong()) * 1000 + fracMs;
        }
        if (!times.isEmpty()) {
            const QString body = line.mid(end).trimmed();
            for (qint64 t : times) timed.push_back({t, body});
        } else if (!meta.match(line).hasMatch()) {
            plain.push_back({-1, line});
        }
    }
    if (!timed.isEmpty()) {
        std::stable_sort(timed.begin(), timed.end(), [](const LyricLine &a, const LyricLine &b) { return a.ms < b.ms; });
        out.lines = timed;
        out.synced = true;
    } else {
        while (!plain.isEmpty() && plain.front().text.isEmpty()) plain.pop_front();
        while (!plain.isEmpty() && plain.back().text.isEmpty()) plain.pop_back();
        out.lines = plain;
    }
    return out;
}

Lyrics LyricsService::fromLrclibJson(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    Lyrics l;
    if (o.value("instrumental").toBool()) { l.instrumental = true; return l; }
    const QString synced = o.value("syncedLyrics").toString();
    l = parse(synced.isEmpty() ? o.value("plainLyrics").toString() : synced);
    return l;
}

QString LyricsService::cacheFile(const Track &t)
{
    const QByteArray key = (t.displayArtist() + '\x1f' + t.displayTitle() + '\x1f' + t.album).toLower().toUtf8();
    return Paths::dataDir() + "/lyrics/" + QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex() + ".lrc";
}

Lyrics LyricsService::local(const Track &t) const
{
    auto readText = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    const QFileInfo fi(t.path);
    const QString lrc = fi.absolutePath() + "/" + fi.completeBaseName() + ".lrc";
    if (QFileInfo::exists(lrc)) { Lyrics l = parse(readText(lrc)); l.source = "file"; if (!l.isEmpty()) return l; }
    const QString tag = TagIO::embeddedLyrics(t.path);
    if (!tag.trimmed().isEmpty()) { Lyrics l = parse(tag); l.source = "tag"; if (!l.isEmpty()) return l; }
    const QString cached = cacheFile(t);
    if (QFileInfo::exists(cached)) {
        const QString text = readText(cached);
        Lyrics l;
        if (text.trimmed() == QLatin1String("[instrumental]")) l.instrumental = true;
        else if (text.trimmed() != QLatin1String("[none]")) l = parse(text);
        l.source = "cache";
        return l;   // a cached "[none]" stops us asking LRCLIB again
    }
    return {};
}

void LyricsService::fetch(const Track &t)
{
    m_pending = t.id;
    if (!enabled() || t.format == QLatin1String("stream")) { emit ready(t.id, {}); return; }
    Lyrics l = local(t);
    if (!l.source.isEmpty()) { emit ready(t.id, l); return; }
    if (!onlineEnabled() || t.title.isEmpty() || t.artist.isEmpty()) { emit ready(t.id, {}); return; }

    QUrl url(QStringLiteral("https://lrclib.net/api/get"));
    QUrlQuery q;
    q.addQueryItem("artist_name", t.displayArtist());
    q.addQueryItem("track_name", t.title);
    if (!t.album.isEmpty()) q.addQueryItem("album_name", t.album);
    if (t.durationMs > 0) q.addQueryItem("duration", QString::number(qRound(t.durationMs / 1000.0)));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HaloSync/0.2 (open-source music manager)"));
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_net.get(req);
    const Track track = t;
    connect(reply, &QNetworkReply::finished, this, [this, reply, track] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        Lyrics l;
        QString cacheText;
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray body = reply->readAll();
            l = fromLrclibJson(body);
            const QJsonObject o = QJsonDocument::fromJson(body).object();
            cacheText = l.instrumental ? QStringLiteral("[instrumental]")
                        : o.value("syncedLyrics").toString(o.value("plainLyrics").toString());
            if (cacheText.trimmed().isEmpty()) cacheText = QStringLiteral("[none]");
        } else if (status == 404) {
            cacheText = QStringLiteral("[none]");     // LRCLIB doesn't have it; don't ask again
        } else {
            logLine("lyrics", "LRCLIB: " + reply->errorString());
        }
        if (!cacheText.isEmpty()) {
            QDir().mkpath(QFileInfo(cacheFile(track)).absolutePath());
            QFile f(cacheFile(track));
            if (f.open(QIODevice::WriteOnly)) f.write(cacheText.toUtf8());
        }
        l.source = QStringLiteral("lrclib");
        emit ready(track.id, l);
    });
}

} // namespace halo
