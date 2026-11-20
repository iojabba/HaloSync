#include "Database.h"
#include "DeviceDetect.h"
#include "Discover.h"
#include "Library.h"
#include "Paths.h"
#include "Radio.h"
#include "Reports.h"
#include "fixtures.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace halo;

class TestFeatures : public QObject {
    Q_OBJECT
    QTemporaryDir data, music;
    std::unique_ptr<Database> db;
    std::unique_ptr<Library> lib;

    Track byTitleFormat(const QString &title, const QString &fmt)
    {
        for (const Track &t : db->allTracks()) if (t.title == title && t.format == fmt) return t;
        return {};
    }

private slots:
    void initTestCase()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg is needed to generate test audio");
        Paths::init(data.path());
        const QString m = music.path();
        QVERIFY(makeTone(m + "/a/song.flac", 3, {{"TITLE", "Song"}, {"ARTIST", "Band"}, {"ALBUM", "LP"}}));
        QVERIFY(makeTone(m + "/a/song.mp3", 3, {{"title", "Song"}, {"artist", "Band"}, {"album", "LP"}}));
        QVERIFY(QFile::copy(m + "/a/song.mp3", m + "/copy/song.mp3") || (QDir().mkpath(m + "/copy") && QFile::copy(m + "/a/song.mp3", m + "/copy/song.mp3")));
        QVERIFY(makeTone(m + "/a/other.mp3", 5, {{"title", "Other"}, {"artist", "Band"}, {"album", "LP"}}));
        db = std::make_unique<Database>();
        QVERIFY(db->open(data.path() + "/lib.sqlite"));
        lib = std::make_unique<Library>(db.get());
        db->addFolder(QFileInfo(m).canonicalFilePath());
        lib->rescanBlocking();
        QCOMPARE(db->allTracks().size(), 4);
    }

    void duplicateReviewFindsIdenticalAndFormats()
    {
        const auto groups = Reports::duplicateReview(db->allTracks(), db.get());
        // The two identical MP3s form one group; the FLAC is "same song, other format" but
        // only one non-identical copy is left, so it isn't a group of its own.
        QCOMPARE(groups.size(), 1);
        QVERIFY(groups[0].identical);
        QCOMPARE(groups[0].tracks.size(), 2);
        // Without the identical copy, FLAC vs MP3 is a format duplicate and FLAC is the one to keep.
        TrackList lib;
        for (const Track &t : db->allTracks()) if (!t.path.contains("/copy/")) lib << t;
        const auto g2 = Reports::duplicateReview(lib, db.get());
        QCOMPARE(g2.size(), 1);
        QVERIFY(!g2[0].identical);
        QCOMPARE(g2[0].tracks[g2[0].best].format, QString("flac"));
        QVERIFY(!db->cachedHash(groups[0].tracks[0].path, groups[0].tracks[0].sizeBytes, groups[0].tracks[0].mtime).isEmpty());
    }

    void mergeKeepsStats()
    {
        const Track flac = byTitleFormat("Song", "flac");
        const Track mp3 = byTitleFormat("Song", "mp3");
        db->recordPlay(mp3.id);
        db->recordPlay(mp3.id);
        db->setRating(mp3.id, Like);
        const qint64 pl = db->createPlaylist("P");
        db->setPlaylistItems(pl, {mp3.id});
        db->mergeTrackStats(flac.id, {mp3.id});
        const auto kept = db->track(flac.id);
        QCOMPARE(kept->playCount, 2);
        QCOMPARE(kept->rating, int(Like));
        QCOMPARE(db->playlistItems(pl), QVector<qint64>{flac.id});
        int hist = 0;
        for (const auto &h : db->history()) if (h.first == flac.id) ++hist;
        QCOMPARE(hist, 2);
    }

    void discoverShelves()
    {
        const qint64 now = 1800000000;
        TrackList lib;
        auto mk = [&](qint64 id, QString artist, QString album, QString genre, int plays, qint64 last, int rating, qint64 added) {
            Track t; t.id = id; t.artist = artist; t.album = album; t.genre = genre; t.title = album + QString::number(id);
            t.playCount = plays; t.lastPlayed = last; t.rating = rating; t.dateAdded = added; lib << t; };
        mk(1, "Fav", "Hits", "Rock", 30, now - 86400, 0, now - 400 * 86400);
        mk(2, "Old", "Gold", "Jazz", 9, now - 100 * 86400, 1, now - 400 * 86400);
        mk(3, "New", "Fresh", "Pop", 0, 0, 0, now - 20 * 86400);
        mk(4, "Rare", "Deep", "Rock", 0, 0, 0, now - 2 * 86400);
        QVector<QPair<qint64, qint64>> hist;
        for (int i = 0; i < 5; ++i) hist.push_back({1, now - i * 3600});
        hist.push_back({2, now - 365 * 86400});      // one year ago today
        const auto shelves = Discover::shelves(lib, hist, now);
        QStringList titles;
        for (const auto &s : shelves) titles << s.title;
        QVERIFY2(titles.contains("your month") && titles.contains("back in rotation") && titles.contains("you haven't played")
                 && titles.contains("more like your favourites"), qPrintable(titles.join(", ")));
        for (const auto &s : shelves) {
            if (s.title == "your month") QCOMPARE(s.items[0].label, QString("Hits"));
            if (s.title == "back in rotation") QCOMPARE(s.items[0].tracks[0].id, 2);
            if (s.title == "you haven't played") { QCOMPARE(s.items.size(), 1); QCOMPARE(s.items[0].label, QString("Fresh")); }  // Deep is too new
            if (s.title == "more like your favourites") QCOMPARE(s.items[0].label, QString("Rare"));
        }
    }

    void zuneModels()
    {
        QCOMPARE(DeviceDetect::zuneModel("063E", "MTP USB Device"), QString("Zune HD"));
        QCOMPARE(DeviceDetect::zuneModel("0710", ""), QString("Zune"));
        QCOMPARE(DeviceDetect::zuneModel("0999", "Zune 80"), QString("Zune 80"));
        QVERIFY(DeviceDetect::zuneModel("07a5", "Microsoft Wireless Mouse").isEmpty());
        QVERIFY(!DeviceDetect::zuneNotice().isEmpty());
    }

    void radioParsing()
    {
        const QByteArray json =
            "[{\"stationuuid\":\"u1\",\"name\":\" Jazz FM \",\"url\":\"http://a/x.pls\","
            "\"url_resolved\":\"http://a/stream\",\"tags\":\"jazz,smooth\",\"country\":\"UK\","
            "\"codec\":\"MP3\",\"bitrate\":128},{\"name\":\"no url\"}]";
        const auto st = RadioBrowser::parseStations(json);
        QCOMPARE(st.size(), 1);
        QCOMPARE(st[0].name, QString("Jazz FM"));
        QCOMPARE(st[0].url, QString("http://a/stream"));
        QCOMPARE(RadioBrowser::streamUrlsFromPlaylist("[playlist]\nFile1=http://s/1\nTitle1=x\nFile2 = https://s/2\n"),
                 QStringList({"http://s/1", "https://s/2"}));
        QCOMPARE(RadioBrowser::streamUrlsFromPlaylist("#EXTM3U\n#EXTINF:-1,Station\nhttp://s/live\n"), QStringList{"http://s/live"});
        RadioStation s = st[0];
        QVERIFY(db->addRadioStation(s) > 0);
        QVERIFY(db->addRadioStation(s) > 0);       // same URL: no second copy
        QCOMPARE(db->radioStations().size(), 1);
        db->removeRadioStation(db->radioStations()[0].id);
        QCOMPARE(db->radioStations().size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestFeatures)
#include "test_features.moc"
