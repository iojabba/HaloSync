#include "Radio.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>

namespace halo {

RadioBrowser::RadioBrowser(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<halo::RadioStation>();
    qRegisterMetaType<QVector<halo::RadioStation>>();
}

void RadioBrowser::search(const QString &query, bool byTag, int limit)
{
    // all.api.radio-browser.info resolves to any of the directory's mirrors.
    QUrl url(QStringLiteral("https://all.api.radio-browser.info/json/stations/search"));
    QUrlQuery q;
    if (!query.trimmed().isEmpty()) q.addQueryItem(byTag ? "tag" : "name", query.trimmed());
    q.addQueryItem("limit", QString::number(limit));
    q.addQueryItem("hidebroken", "true");
    q.addQueryItem("order", "clickcount");
    q.addQueryItem("reverse", "true");
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HaloSync/0.2"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit failed(reply->errorString()); return; }
        emit results(parseStations(reply->readAll()));
    });
}

QVector<RadioStation> RadioBrowser::parseStations(const QByteArray &json)
{
    QVector<RadioStation> out;
    for (const auto &v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        RadioStation s;
        s.uuid = o["stationuuid"].toString();
        s.name = o["name"].toString().trimmed();
        s.url = o["url_resolved"].toString();
        if (s.url.isEmpty()) s.url = o["url"].toString();
        s.homepage = o["homepage"].toString();
        s.favicon = o["favicon"].toString();
        s.tags = o["tags"].toString();
        s.country = o["country"].toString();
        s.codec = o["codec"].toString();
        s.bitrate = o["bitrate"].toInt();
        if (!s.url.isEmpty() && !s.name.isEmpty()) out << s;
    }
    return out;
}

QStringList RadioBrowser::streamUrlsFromPlaylist(const QString &text)
{
    QStringList out;
    static const QRegularExpression pls(QStringLiteral("^File\\d+\\s*=\\s*(.+)$"), QRegularExpression::CaseInsensitiveOption);
    for (QString line : text.split(QRegularExpression(QStringLiteral("\\r?\\n")))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith('[')) continue;
        const auto m = pls.match(line);
        const QString url = m.hasMatch() ? m.captured(1).trimmed() : line;
        if (url.startsWith("http://", Qt::CaseInsensitive) || url.startsWith("https://", Qt::CaseInsensitive)) out << url;
    }
    return out;
}

} // namespace halo
