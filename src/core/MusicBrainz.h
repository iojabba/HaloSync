#pragma once
#include <QObject>
#include <QImage>
#include <QNetworkAccessManager>
#include <QVector>

namespace halo {

struct MbRelease {
    QString id, title, artist, date, country;
    int trackCount = 0;
};

struct MbTrack {
    int disc = 1, position = 0;
    QString title, artist;
};

struct MbReleaseDetail {
    QString id, title, artist;
    int year = 0;
    QVector<MbTrack> tracks;
};

// Minimal MusicBrainz + Cover Art Archive client for "Find album info" (SPEC §2.4, §3.5).
class MusicBrainz : public QObject {
    Q_OBJECT
public:
    explicit MusicBrainz(QObject *parent = nullptr);
    void searchReleases(const QString &artist, const QString &album);
    void fetchRelease(const QString &releaseId);
    void fetchCover(const QString &releaseId);

    // Parsers, public for tests.
    static QVector<MbRelease> parseSearch(const QByteArray &json);
    static MbReleaseDetail parseRelease(const QByteArray &json);

signals:
    void searchFinished(const QVector<halo::MbRelease> &results);
    void releaseFinished(const halo::MbReleaseDetail &release);
    void coverFinished(const QImage &image);
    void failed(const QString &message);

private:
    QNetworkRequest request(const QUrl &url) const;
    QNetworkAccessManager m_net;
};

} // namespace halo
