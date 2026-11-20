#include "ArtFinder.h"
#include "Lyrics.h"
#include <QTest>

using namespace halo;

class TestOnline : public QObject {
    Q_OBJECT
private slots:
    void parsesLrc()
    {
        const Lyrics l = LyricsService::parse("[ar:Someone]\n[ti:Song]\n[00:12.50]First line\n[00:05.1]Intro\n"
                                              "[01:02.345][01:30.00]Chorus\nnot timed\n");
        QVERIFY(l.synced);
        QCOMPARE(l.lines.size(), 4);
        QCOMPARE(l.lines[0].ms, 5100);
        QCOMPARE(l.lines[0].text, QString("Intro"));
        QCOMPARE(l.lines[1].ms, 12500);
        QCOMPARE(l.lines[2].ms, 62345);
        QCOMPARE(l.lines[3].text, QString("Chorus"));
        QCOMPARE(l.lineAt(0), -1);
        QCOMPARE(l.lineAt(6000), 0);
        QCOMPARE(l.lineAt(70000), 2);
        QCOMPARE(l.lineAt(999999), 3);
    }
    void parsesPlain()
    {
        const Lyrics l = LyricsService::parse("\nOne\nTwo\n\nThree\n\n");
        QVERIFY(!l.synced);
        QCOMPARE(l.lines.size(), 4);
        QCOMPARE(l.lineAt(1000), -1);
    }
    void lrclibJson()
    {
        Lyrics l = LyricsService::fromLrclibJson(R"({"id":1,"instrumental":false,"plainLyrics":"a\nb","syncedLyrics":"[00:01.00]a\n[00:02.00]b"})");
        QVERIFY(l.synced);
        QCOMPARE(l.lines.size(), 2);
        l = LyricsService::fromLrclibJson(R"({"plainLyrics":"a\nb","syncedLyrics":null})");
        QVERIFY(!l.synced);
        QCOMPARE(l.lines.size(), 2);
        l = LyricsService::fromLrclibJson(R"({"instrumental":true})");
        QVERIFY(l.instrumental);
        QVERIFY(!l.isEmpty());
    }
    void artMatching()
    {
        QCOMPARE(ArtFinder::normalize("The Wall (Deluxe Edition)"), QString("wall"));
        QCOMPARE(ArtFinder::normalize("Simon & Garfunkel"), QString("simon and garfunkel"));
        QCOMPARE(ArtFinder::normalize("AC/DC"), ArtFinder::normalize("AC DC"));
    }
    void artCandidatesSkipSharedFolders()
    {
        auto mk = [](QString path, QString album, bool art) {
            Track t; t.path = path; t.album = album; t.artist = "A"; t.hasArt = art; return t; };
        const TrackList lib = {mk("/m/x/1.mp3", "X", false), mk("/m/x/2.mp3", "X", false),
                               mk("/m/mixed/1.mp3", "Y", false), mk("/m/mixed/2.mp3", "Z", false),
                               mk("/m/w/1.mp3", "W", true)};
        const auto c = ArtFinder::candidates(lib);
        QCOMPARE(c.size(), 1);
        QCOMPARE(c[0].album, QString("X"));
    }
};

QTEST_GUILESS_MAIN(TestOnline)
#include "test_online.moc"
