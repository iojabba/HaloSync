#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QVector>

namespace halo {

struct RadioStation {
    qint64 id = 0;          // > 0 when saved as a favourite
    QString uuid;           // Radio Browser station id
    QString name, url, homepage, favicon, tags, country, codec;
    int bitrate = 0;
};

// Internet radio (SPEC §5, 2b). Station search uses the Radio Browser
// community directory (api.radio-browser.info, free and open). Station files
// (.m3u, .m3u8, .pls) and plain stream URLs can be added by hand.
class RadioBrowser : public QObject {
    Q_OBJECT
public:
    explicit RadioBrowser(QObject *parent = nullptr);
    // Empty query: the most popular stations. `byTag` searches genres/tags.
    void search(const QString &query, bool byTag = false, int limit = 60);

    static QVector<RadioStation> parseStations(const QByteArray &json);
    // Stream URLs in an .m3u/.pls station file (or the URL itself if it's a stream).
    static QStringList streamUrlsFromPlaylist(const QString &text);

signals:
    void results(const QVector<halo::RadioStation> &stations);
    void failed(const QString &message);

private:
    QNetworkAccessManager m_net;
};

} // namespace halo

Q_DECLARE_METATYPE(halo::RadioStation)
