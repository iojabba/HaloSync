#include "AutoPlaylist.h"
#include <QTest>

using namespace halo;

class TestAutoPlaylist : public QObject {
    Q_OBJECT
    TrackList lib;
    const qint64 now = 1'700'000'000;

    Track mk(qint64 id, QString artist, QString album, QString genre, int year, int rating, int plays, qint64 addedDaysAgo, qint64 ms)
    {
        Track t;
        t.id = id;
        t.path = QStringLiteral("/m/%1.mp3").arg(id);
        t.title = QStringLiteral("Song %1").arg(id);
        t.artist = t.albumArtist = artist;
        t.album = album;
        t.genre = genre;
        t.year = year;
        t.rating = rating;
        t.playCount = plays;
        t.dateAdded = now - addedDaysAgo * 86400;
        t.durationMs = ms;
        t.trackNo = int(id);
        return t;
    }

private slots:
    void initTestCase()
    {
        lib = {mk(1, "Alpha", "A1", "Pop", 1999, Like, 10, 2, 180000),
               mk(2, "Alpha", "A1", "Pop", 1999, Unrated, 0, 40, 200000),
               mk(3, "Beta", "B1", "Rock", 2005, Dislike, 3, 1, 240000),
               mk(4, "Gamma", "G1", "Jazz", 2012, Like, 25, 100, 300000),
               mk(5, "Beta", "B2", "rock", 2010, Unrated, 1, 5, 120000)};
    }

    void emptyRulesMatchAll()
    {
        AutoPlaylistSpec s;
        QCOMPARE(s.evaluate(lib, now).size(), 5);
    }

    void genreSemicolonIsOr()
    {
        AutoPlaylistSpec s;
        AutoRule r;
        r.field = AutoRule::Genre;
        r.values = {"Pop;Rock"};
        s.rules = {r};
        QCOMPARE(s.evaluate(lib, now).size(), 4);   // case-insensitive "rock"
    }

    void excludeAndCombine()
    {
        AutoPlaylistSpec s;
        AutoRule g; g.field = AutoRule::Genre; g.values = {"Pop;Rock"};
        AutoRule x; x.field = AutoRule::AlbumArtist; x.mode = AutoRule::Exclude; x.values = {"Beta"};
        s.rules = {g, x};
        const TrackList r = s.evaluate(lib, now);
        QCOMPARE(r.size(), 2);
        for (const Track &t : r) QCOMPARE(t.albumArtist, QString("Alpha"));
    }

    void ratingPlaysYearAdded()
    {
        AutoPlaylistSpec s;
        AutoRule r; r.field = AutoRule::Rating; r.rating = AutoRule::RatingLikeOrUnrated;
        s.rules = {r};
        QCOMPARE(s.evaluate(lib, now).size(), 4);

        AutoRule p; p.field = AutoRule::Plays; p.playsOp = AutoRule::PlaysNever;
        s.rules = {p};
        QCOMPARE(s.evaluate(lib, now).size(), 1);
        p.playsOp = AutoRule::PlaysMoreThan; p.playsN = 5;
        s.rules = {p};
        QCOMPARE(s.evaluate(lib, now).size(), 2);

        AutoRule y; y.field = AutoRule::Year; y.min = 2000; y.max = 2010;
        s.rules = {y};
        QCOMPARE(s.evaluate(lib, now).size(), 2);

        AutoRule a; a.field = AutoRule::AddedInLastDays; a.min = 7;
        s.rules = {a};
        QCOMPARE(s.evaluate(lib, now).size(), 3);
    }

    void arrangeAndLimit()
    {
        AutoPlaylistSpec s;
        s.arrange = AutoPlaylistSpec::ByMostPlayed;
        s.limit = AutoPlaylistSpec::LimitSongs;
        s.limitCount = 2;
        TrackList r = s.evaluate(lib, now);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].id, 4);
        QCOMPARE(r[1].id, 1);

        s.arrange = AutoPlaylistSpec::ByYear;
        s.limit = AutoPlaylistSpec::LimitMinutes;
        s.limitCount = 7;   // 420 s: 180 + 200 fit, 240 doesn't
        r = s.evaluate(lib, now);
        QCOMPARE(r.size(), 2);

        s.arrange = AutoPlaylistSpec::Random;
        s.limit = AutoPlaylistSpec::NoLimit;
        s.randomSeed = 42;
        QCOMPARE(s.evaluate(lib, now).size(), 5);
    }

    void jsonRoundTrip()
    {
        AutoPlaylistSpec s;
        AutoRule g; g.field = AutoRule::Genre; g.values = {"Pop", "Rock"}; g.mode = AutoRule::Exclude;
        AutoRule y; y.field = AutoRule::Year; y.min = 1990; y.max = 1999;
        s.rules = {g, y};
        s.arrange = AutoPlaylistSpec::ByGenre;
        s.limit = AutoPlaylistSpec::LimitMinutes;
        s.limitCount = 60;
        const AutoPlaylistSpec back = AutoPlaylistSpec::fromJsonString(s.toJsonString());
        QCOMPARE(back.rules.size(), 2);
        QCOMPARE(back.rules[0].values, QStringList({"Pop", "Rock"}));
        QCOMPARE(int(back.rules[0].mode), int(AutoRule::Exclude));
        QCOMPARE(back.rules[1].max, 1999);
        QCOMPARE(back.arrange, int(AutoPlaylistSpec::ByGenre));
        QCOMPARE(back.limitCount, 60);
        QCOMPARE(back.evaluate(lib, now).size(), s.evaluate(lib, now).size());
    }
};

QTEST_GUILESS_MAIN(TestAutoPlaylist)
#include "test_autoplaylist.moc"
