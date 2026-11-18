#include "Database.h"
#include "Library.h"
#include "Paths.h"
#include "Reports.h"
#include "fixtures.h"
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace halo;

class TestLibrary : public QObject {
    Q_OBJECT
    QTemporaryDir data, music;
    std::unique_ptr<Database> db;
    std::unique_ptr<Library> lib;

    std::optional<Track> byTitle(const QString &title)
    {
        for (const Track &t : db->allTracks()) if (t.title == title) return t;
        return std::nullopt;
    }

private slots:
    void initTestCase()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg is needed to generate test audio");
        Paths::init(data.path());
        const QString m = music.path();
        QVERIFY(makeTone(m + "/Alpha/First/01.mp3", 2, {{"title", "One"}, {"artist", "Alpha"}, {"album_artist", "Alpha"},
                         {"album", "First"}, {"genre", "Pop"}, {"date", "2001"}, {"track", "1/2"}, {"composer", "Writer"}}));
        QVERIFY(makeTone(m + "/Alpha/First/02.flac", 3, {{"TITLE", "Two"}, {"ARTIST", "Alpha feat. Beta"}, {"ALBUMARTIST", "Alpha"},
                         {"ALBUM", "First"}, {"GENRE", "Pop"}, {"DATE", "2001"}, {"TRACKNUMBER", "2"}, {"DISCNUMBER", "1"},
                         {"REPLAYGAIN_TRACK_GAIN", "-6.50 dB"}}));
        QVERIFY(makeTone(m + "/Gamma/03.ogg", 2, {{"TITLE", "Three"}, {"ARTIST", "Gamma"}, {"ALBUM", "Live"}, {"GENRE", "Jazz"}}));
        QVERIFY(makeTone(m + "/Gamma/dupe.m4a", 2, {{"title", "Three"}, {"artist", "Gamma"}, {"album", "Live"}}));
        QFile(m + "/notes.txt").open(QIODevice::WriteOnly);
        db = std::make_unique<Database>();
        QVERIFY(db->open(data.path() + "/lib.sqlite"));
        lib = std::make_unique<Library>(db.get());
    }

    void addFolderValidates()
    {
        QVERIFY(!lib->addFolder(music.path() + "/does-not-exist").isEmpty());
        db->addFolder(QFileInfo(music.path()).canonicalFilePath());
        QVERIFY(!lib->addFolder(music.path() + "/Alpha").isEmpty());   // nested in a monitored folder
    }

    void scanReadsTags()
    {
        const ScanResult r = lib->rescanBlocking();
        QCOMPARE(r.changed.size(), 4);
        QCOMPARE(db->allTracks().size(), 4);
        auto one = byTitle("One");
        QVERIFY(one);
        QCOMPARE(one->albumArtist, QString("Alpha"));
        QCOMPARE(one->album, QString("First"));
        QCOMPARE(one->year, 2001);
        QCOMPARE(one->trackNo, 1);
        QCOMPARE(one->composer, QString("Writer"));
        QVERIFY(one->durationMs > 1500 && one->durationMs < 2600);
        QCOMPARE(one->format, QString("mp3"));
        auto two = byTitle("Two");
        QVERIFY(two);
        QCOMPARE(two->artist, QString("Alpha feat. Beta"));
        QCOMPARE(two->albumArtist, QString("Alpha"));
        QCOMPARE(two->discNo, 1);
        QCOMPARE(two->rgTrackGain, -6.5);
        QCOMPARE(two->format, QString("flac"));
    }

    void rescanIsIncremental()
    {
        const ScanResult r = lib->rescanBlocking();
        QCOMPARE(r.changed.size(), 0);
        QCOMPARE(r.unchanged, 4);
    }

    void ratingAndPlaysSurviveTagEdit()
    {
        auto one = byTitle("One");
        db->setRating(one->id, Like);
        db->recordPlay(one->id);
        db->recordPlay(one->id);
        TagEdit e;
        e.title = QStringLiteral("One (Remastered)");
        e.genre = QStringLiteral("Synthpop");
        QStringList errors;
        QVERIFY2(lib->editTags({one->id}, e, &errors), qPrintable(errors.join(';')));
        auto edited = db->track(one->id);
        QVERIFY(edited);
        QCOMPARE(edited->title, QString("One (Remastered)"));
        QCOMPARE(edited->genre, QString("Synthpop"));
        QCOMPARE(edited->rating, int(Like));
        QCOMPARE(edited->playCount, 2);
        QVERIFY(edited->lastPlayed > 0);
        QCOMPARE(db->recentTrackIds(5).value(0), one->id);
        // Written into the file, not just the DB:
        auto reread = TagIO::read(edited->path);
        QCOMPARE(reread->title, QString("One (Remastered)"));
    }

    void mergeGenre()
    {
        const int n = lib->merge("genre", "Synthpop", "Pop");
        QCOMPARE(n, 1);
        int pop = 0;
        for (const Track &t : db->allTracks()) if (t.genre == "Pop") ++pop;
        QCOMPARE(pop, 2);
    }

    void reports()
    {
        const auto d = Reports::duplicates(db->allTracks());
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].tracks.size(), 2);
        QCOMPARE(Reports::missingArt(db->allTracks()).size(), 4);
        QImage img(64, 64, QImage::Format_RGB32);
        img.fill(Qt::red);
        QString err;
        QVERIFY2(lib->pasteAlbumArt("Alpha", "First", img, &err), qPrintable(err));
        QVERIFY(QFile::exists(music.path() + "/Alpha/First/folder.jpg"));
        QCOMPARE(Reports::missingArt(db->allTracks()).size(), 2);
    }

    void playlists()
    {
        const qint64 id = db->createPlaylist("Mix");
        QVERIFY(id > 0);
        QCOMPARE(db->createPlaylist("mix"), qint64(-1));   // names are unique, case-insensitive
        QVector<qint64> ids;
        for (const Track &t : db->allTracks()) ids << t.id;
        db->setPlaylistItems(id, ids);
        db->appendToPlaylist(id, {ids.first()});
        QCOMPARE(lib->playlistTracks(id).size(), 5);
        QVERIFY(db->renamePlaylist(id, "Mix 2"));
        db->addPin({0, "playlist", QString::number(id), "Mix 2", 0});
        QCOMPARE(db->pins().size(), 1);
        db->deletePlaylist(id);
        QCOMPARE(db->pins().size(), 0);
    }

    void removedFilesLeaveCollection()
    {
        QVERIFY(QFile::remove(music.path() + "/Gamma/dupe.m4a"));
        const ScanResult r = lib->rescanBlocking();
        QCOMPARE(r.removed.size(), 1);
        QCOMPARE(db->allTracks().size(), 3);
    }

    void removeFolderKeepsFiles()
    {
        const auto folders = db->folders();
        QCOMPARE(folders.size(), 1);
        lib->removeFolder(folders[0].id);
        QCOMPARE(db->allTracks().size(), 0);
        QVERIFY(QFile::exists(music.path() + "/Alpha/First/01.mp3"));
    }
};

QTEST_GUILESS_MAIN(TestLibrary)
#include "test_library.moc"
