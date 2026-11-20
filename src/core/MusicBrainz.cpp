#include "MusicBrainz.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

namespace halo {

namespace {
QString artistCredit(const QJsonArray &credit)
{
    QString s;
    for (const auto &v : credit) {
        const QJsonObject c = v.toObject();
        s += c["name"].toString(c["artist"].toObject()["name"].toString());
        s += c["joinphrase"].toString();
    }
    return s;
}

QString luceneEscape(QString s)
{
    static const QString special = QStringLiteral("+-&|!(){}[]^\"~*?:\\/");
    QString out;
    for (QChar c : s) { if (special.contains(c)) out += '\\'; out += c; }
    return out;
}
} // namespace

MusicBrainz::MusicBrainz(QObject *parent) : QObject(parent) {}

QNetworkRequest MusicBrainz::request(const QUrl &url) const
{
    QNetworkRequest r(url);
    // MusicBrainz asks clients to identify themselves.
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HaloSync/0.2 (open-source music manager)"));
    r.setRawHeader("Accept", "application/json");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(20000);
    return r;
}

void MusicBrainz::searchReleases(const QString &artist, const QString &album)
{
    QStringList terms;
    if (!album.trimmed().isEmpty()) terms << QStringLiteral("release:\"%1\"").arg(luceneEscape(album.trimmed()));
    if (!artist.trimmed().isEmpty()) terms << QStringLiteral("artist:\"%1\"").arg(luceneEscape(artist.trimmed()));
    if (terms.isEmpty()) { emit failed(QStringLiteral("Enter an artist or album name.")); return; }
    QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release/"));
    QUrlQuery q;
    q.addQueryItem("query", terms.join(" AND "));
    q.addQueryItem("fmt", "json");
    q.addQueryItem("limit", "25");
    url.setQuery(q);
    QNetworkReply *reply = m_net.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit failed(reply->errorString()); return; }
        emit searchFinished(parseSearch(reply->readAll()));
    });
}

void MusicBrainz::fetchRelease(const QString &releaseId)
{
    QUrl url(QStringLiteral("https://musicbrainz.org/ws/2/release/%1").arg(releaseId));
    QUrlQuery q;
    q.addQueryItem("inc", "recordings+artist-credits");
    q.addQueryItem("fmt", "json");
    url.setQuery(q);
    QNetworkReply *reply = m_net.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit failed(reply->errorString()); return; }
        emit releaseFinished(parseRelease(reply->readAll()));
    });
}

void MusicBrainz::fetchCover(const QString &releaseId)
{
    const QUrl url(QStringLiteral("https://coverartarchive.org/release/%1/front-500").arg(releaseId));
    QNetworkReply *reply = m_net.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        QImage img;
        if (reply->error() == QNetworkReply::NoError) img.loadFromData(reply->readAll());
        if (img.isNull()) { emit failed(QStringLiteral("No cover art found for this release.")); return; }
        emit coverFinished(img);
    });
}

QVector<MbRelease> MusicBrainz::parseSearch(const QByteArray &json)
{
    QVector<MbRelease> out;
    const QJsonArray releases = QJsonDocument::fromJson(json).object()["releases"].toArray();
    for (const auto &v : releases) {
        const QJsonObject r = v.toObject();
        MbRelease m;
        m.id = r["id"].toString();
        m.title = r["title"].toString();
        m.artist = artistCredit(r["artist-credit"].toArray());
        m.date = r["date"].toString();
        m.country = r["country"].toString();
        m.trackCount = r["track-count"].toInt();
        out.push_back(m);
    }
    return out;
}

MbReleaseDetail MusicBrainz::parseRelease(const QByteArray &json)
{
    const QJsonObject r = QJsonDocument::fromJson(json).object();
    MbReleaseDetail d;
    d.id = r["id"].toString();
    d.title = r["title"].toString();
    d.artist = artistCredit(r["artist-credit"].toArray());
    d.year = r["date"].toString().left(4).toInt();
    for (const auto &mv : r["media"].toArray()) {
        const QJsonObject medium = mv.toObject();
        const int disc = medium["position"].toInt(1);
        for (const auto &tv : medium["tracks"].toArray()) {
            const QJsonObject t = tv.toObject();
            MbTrack mt;
            mt.disc = disc;
            mt.position = t["position"].toInt();
            mt.title = t["title"].toString();
            const QJsonArray credit = t["artist-credit"].toArray();
            mt.artist = credit.isEmpty() ? d.artist : artistCredit(credit);
            d.tracks.push_back(mt);
        }
    }
    return d;
}

} // namespace halo
