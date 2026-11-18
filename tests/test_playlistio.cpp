#include "PlaylistIO.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace halo;

class TestPlaylistIO : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    PlaylistFile sample() const
    {
        PlaylistFile pl{QStringLiteral("Road trip & more"), {}};
        pl.entries.push_back({dir.path() + "/Music/Artist/Album/01 Fïrst <song>.mp3", "Fïrst <song>", "Artist", 185000});
        pl.entries.push_back({dir.path() + "/Music/Other/02 Second.flac", "Second", "Other", 61000});
        return pl;
    }

private slots:
    void roundTrip_data()
    {
        QTest::addColumn<QString>("ext");
        QTest::addColumn<bool>("relative");
        for (const char *e : {"m3u", "m3u8", "pls", "xspf", "zpl", "wpl"}) {
            QTest::newRow(QByteArray(e).append("-abs").constData()) << QString(e) << false;
            QTest::newRow(QByteArray(e).append("-rel").constData()) << QString(e) << true;
        }
    }

    void roundTrip()
    {
        QFETCH(QString, ext);
        QFETCH(bool, relative);
        const QString file = dir.path() + "/Playlists/test." + ext;
        QString err;
        QVERIFY2(PlaylistIO::write(file, sample(), relative, &err), qPrintable(err));
        auto back = PlaylistIO::read(file, &err);
        QVERIFY2(back.has_value(), qPrintable(err));
        QCOMPARE(back->entries.size(), 2);
        QCOMPARE(back->entries[0].path, sample().entries[0].path);
        QCOMPARE(back->entries[1].path, sample().entries[1].path);
        if (ext != "pls") QCOMPARE(back->name, sample().name);
        QCOMPARE(back->entries[0].title, QString("Fïrst <song>"));
        QCOMPARE(back->entries[0].durationMs / 1000, 185);
    }

    void readsForeignM3u()
    {
        const QString file = dir.path() + "/foreign.m3u";
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#EXTM3U\r\n#EXTINF:200,Band - Tune\r\nsub\\dir\\tune.mp3\r\n\r\nC:\\Music\\x.mp3\r\nfile:///tmp/y%20z.mp3\r\n");
        f.close();
        auto pl = PlaylistIO::read(file);
        QVERIFY(pl);
        QCOMPARE(pl->entries.size(), 3);
        QCOMPARE(pl->entries[0].path, dir.path() + "/sub/dir/tune.mp3");
        QCOMPARE(pl->entries[0].artist, QString("Band"));
        QCOMPARE(pl->entries[0].title, QString("Tune"));
        QCOMPARE(pl->entries[1].path, QString("C:/Music/x.mp3"));
        QCOMPARE(pl->entries[2].path, QString("/tmp/y z.mp3"));
    }

    void rejectsUnknown()
    {
        QString err;
        QVERIFY(!PlaylistIO::write(dir.path() + "/x.txt", sample(), false, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPlaylistIO)
#include "test_playlistio.moc"
