#include "Database.h"
#include "Library.h"
#include "Paths.h"
#include "SyncEngine.h"
#include "fixtures.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace halo;

class TestSync : public QObject {
    Q_OBJECT
    QTemporaryDir data, music, device;
    std::unique_ptr<Database> db;
    std::unique_ptr<Library> lib;
    SyncOptions opts;

    QVector<QPair<QString, TrackList>> noPlaylists() const { return {}; }

private slots:
    void initTestCase()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg is needed to generate test audio");
        Paths::init(data.path());
        const QString m = music.path();
        QVERIFY(makeTone(m + "/a1.mp3", 2, {{"title", "Song: One?"}, {"artist", "AC/DC"}, {"album", "Hi*"}, {"track", "1"}}));
        QVERIFY(makeTone(m + "/a2.flac", 2, {{"TITLE", "Lossless"}, {"ARTIST", "AC/DC"}, {"ALBUM", "Hi*"}, {"TRACKNUMBER", "2"}}));
        QVERIFY(makeTone(m + "/b1.opus", 2, {{"TITLE", "Small"}, {"ARTIST", "Beta"}, {"ALBUM", "Bee"}}));
        db = std::make_unique<Database>();
        QVERIFY(db->open(data.path() + "/lib.sqlite"));
        lib = std::make_unique<Library>(db.get());
        db->addFolder(QFileInfo(m).canonicalFilePath());
        lib->rescanBlocking();
        QCOMPARE(db->allTracks().size(), 3);
        opts.target.codec = "mp3";
        opts.target.bitrateKbps = 128;
    }

    void destPathsAreSafe()
    {
        Track t;
        t.title = "Song: One?";
        t.albumArtist = "AC/DC";
        t.album = "Hi*";
        t.trackNo = 1;
        t.format = "mp3";
        QCOMPARE(SyncEngine::destPathFor(t, opts), QString("Music/AC_DC/Hi_/01 Song_ One_.mp3"));
        t.format = "flac";
        QVERIFY(SyncEngine::needsTranscode(t, opts));
        QVERIFY(SyncEngine::destPathFor(t, opts).endsWith(".mp3"));
    }

    void selectionModes()
    {
        const TrackList all = db->allTracks();
        DeviceProfile d;
        d.mode = "all";
        QCOMPARE(SyncEngine::selectTracks(d, {}, all, {}).size(), 3);
        d.mode = "chosen";
        QCOMPARE(SyncEngine::selectTracks(d, {{"artist", "AC/DC"}}, all, {}).size(), 2);
        QCOMPARE(SyncEngine::selectTracks(d, {{"album", QString("Beta") + QChar(0x1f) + "Bee"}}, all, {}).size(), 1);
        QCOMPARE(SyncEngine::selectTracks(d, {{"genre", "Unknown Genre"}}, all, {}).size(), 3);
        QCOMPARE(SyncEngine::selectTracks(d, {}, all, {}).size(), 0);
    }

    void fullSyncCopiesAndConverts()
    {
        const TrackList all = db->allTracks();
        SyncPlan p = SyncEngine::plan(device.path(), all, opts, true);
        QCOMPARE(p.count(SyncAction::Copy), 1);
        QCOMPARE(p.count(SyncAction::Transcode), 2);   // flac + opus aren't in the device's format list
        QVERIFY(!p.overfilled());
        TrackList pl = {all[0], all[1]};
        const SyncReport r = SyncEngine::execute(device.path(), p, opts, {{"Favs", pl}});
        QVERIFY2(r.failed == 0, qPrintable(r.errors.join('\n')));
        QCOMPARE(r.copied, 1);
        QCOMPARE(r.transcoded, 2);
        QVERIFY(QFile::exists(device.path() + "/Music/AC_DC/Hi_/02 Lossless.mp3"));
        QVERIFY(QFile::exists(device.path() + "/Music/Beta/Bee/Small.mp3"));
        QVERIFY(QFile::exists(SyncEngine::manifestFile(device.path())));
        QFile m3u(device.path() + "/Playlists/Favs.m3u");
        QVERIFY(m3u.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(m3u.readAll());
        QVERIFY(text.contains("../Music/AC_DC/Hi_/01 Song_ One_.mp3"));
        // Converted file is a real MP3 with the tags carried over.
        auto conv = TagIO::read(device.path() + "/Music/AC_DC/Hi_/02 Lossless.mp3");
        QVERIFY(conv);
        QCOMPARE(conv->title, QString("Lossless"));
        QVERIFY(conv->durationMs > 1500);
    }

    void secondSyncIsNoOp()
    {
        const SyncPlan p = SyncEngine::plan(device.path(), db->allTracks(), opts, true);
        QCOMPARE(p.actions.size(), 0);
        QCOMPARE(p.upToDate, 3);
    }

    void deselectedItemsAreRemovedButUserFilesStay()
    {
        QFile mine(device.path() + "/Music/my-own-file.mp3");
        QVERIFY(mine.open(QIODevice::WriteOnly));
        mine.write("not ours");
        mine.close();
        TrackList onlyBeta;
        for (const Track &t : db->allTracks()) if (t.displayAlbumArtist() == "Beta") onlyBeta << t;
        SyncPlan p = SyncEngine::plan(device.path(), onlyBeta, opts, true);
        QCOMPARE(p.count(SyncAction::Delete), 2);
        const SyncReport r = SyncEngine::execute(device.path(), p, opts, {});
        QCOMPARE(r.deleted, 2);
        QVERIFY(!QFile::exists(device.path() + "/Music/AC_DC"));         // empty folders pruned
        QVERIFY(QFile::exists(device.path() + "/Music/my-own-file.mp3")); // never touched
        // Manual mode never deletes.
        p = SyncEngine::plan(device.path(), {}, opts, false);
        QCOMPARE(p.count(SyncAction::Delete), 0);
    }

    void dislikedSkipped()
    {
        TrackList all = db->allTracks();
        all[0].rating = Dislike;
        const SyncPlan p = SyncEngine::plan(device.path(), all, opts, false);
        QCOMPARE(p.skippedDisliked, 1);
    }

    void overfillDetection()
    {
        SyncPlan p;
        p.freeBytes = 100;
        p.bytesToWrite = 150;
        p.bytesToFree = 40;
        QVERIFY(p.overfilled());
        p.bytesToFree = 60;
        QVERIFY(!p.overfilled());
        SyncReport r = SyncEngine::execute(device.path(), SyncPlan{{}, 0, 0, 1000, 0, 10}, opts, {});
        QVERIFY(!r.errors.isEmpty());
    }

    void storageBreakdown()
    {
        const StorageBreakdown b = SyncEngine::storage(device.path());
        QVERIFY(b.valid);
        QVERIFY(b.music > 0);
        QVERIFY(b.total >= b.free);
    }
};

QTEST_GUILESS_MAIN(TestSync)
#include "test_sync.moc"
