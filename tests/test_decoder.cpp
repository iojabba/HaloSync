#include "Decoder.h"
#include "Loudness.h"
#include "TagIO.h"
#include "fixtures.h"
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

using namespace halo;

class TestDecoder : public QObject {
    Q_OBJECT
    QTemporaryDir dir;

    static qint64 countFrames(Decoder &d)
    {
        std::vector<float> buf(4096 * 2);
        qint64 total = 0;
        for (int n; (n = d.read(buf.data(), 4096)) > 0;) total += n;
        return total;
    }

private slots:
    void initTestCase()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg is needed to generate test audio");
        QVERIFY(makeTone(dir.path() + "/t.mp3", 2, {}));
        QVERIFY(makeTone(dir.path() + "/t.flac", 2, {}));
        QVERIFY(makeTone(dir.path() + "/t.m4a", 2, {}));
        QVERIFY(makeTone(dir.path() + "/t.opus", 2, {}));
        // A 997 Hz tone at -20 dBFS (sine amplitude 1/8, then -1.94 dB).
        QVERIFY(makeTone(dir.path() + "/loud.flac", 5, {}, 997, "volume=0.8"));
        QVERIFY(makeTone(dir.path() + "/quiet.flac", 5, {}, 997, "volume=0.2"));
    }

    // Gapless: encoder delay and padding are trimmed, so a 2 s song decodes to 2 s.
    void exactLength_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("mp3") << "t.mp3";
        QTest::newRow("flac") << "t.flac";
        QTest::newRow("aac") << "t.m4a";
        QTest::newRow("opus") << "t.opus";
    }
    void exactLength()
    {
        QFETCH(QString, file);
        Decoder d;
        QString err;
        QVERIFY2(d.open(dir.path() + "/" + file, &err), qPrintable(err));
        const qint64 frames = countFrames(d);
        // Within 5 ms of 2 s (AAC rounds to whole frames of 1024 samples less precisely).
        QVERIFY2(std::llabs(frames - 2 * Decoder::Rate) <= (file.endsWith("m4a") ? 1100 : 240),
                 qPrintable(QString::number(frames)));
        QVERIFY(std::llabs(d.durationMs() - 2000) < 60);
    }

    void seekIsAccurate()
    {
        Decoder d;
        QVERIFY(d.open(dir.path() + "/t.flac"));
        QVERIFY(d.seek(1500));
        const qint64 rest = countFrames(d);
        QVERIFY2(std::llabs(rest - Decoder::Rate / 2) <= 240, qPrintable(QString::number(rest)));
    }

    void missingFileFails()
    {
        Decoder d;
        QString err;
        QVERIFY(!d.open(dir.path() + "/nope.mp3", &err));
        QVERIFY(!err.isEmpty());
    }

    void loudnessMatchesBs1770()
    {
        // Mono 997 Hz sine, amplitude A: loudness = 20*log10(A) - 3.01 LUFS.
        const LoudnessResult r = Loudness::measure(dir.path() + "/loud.flac");
        QVERIFY2(r.ok, qPrintable(r.error));
        const double expected = 20 * std::log10(0.125 * 0.8) - 3.01;
        QVERIFY2(std::abs(r.lufs - expected) < 0.3, qPrintable(QString("%1 vs %2").arg(r.lufs).arg(expected)));
        QVERIFY(std::abs(r.peak - 0.1) < 0.01);
        QVERIFY(std::abs(Loudness::gainDb(r.lufs) - (-18 - expected)) < 0.3);

        // Album loudness of a loud + quiet song lies between the two.
        const LoudnessResult q = Loudness::measure(dir.path() + "/quiet.flac");
        std::vector<double> all = r.blocks;
        all.insert(all.end(), q.blocks.begin(), q.blocks.end());
        const double album = Loudness::integrated(all);
        QVERIFY(album < r.lufs && album > q.lufs);
    }

    void replayGainTagsRoundTrip()
    {
        const QString f = dir.path() + "/loud.flac";
        QVERIFY(TagIO::writeReplayGain(f, -3.25, 0.1, -4.5, 0.2));
        auto t = TagIO::read(f);
        QVERIFY(t);
        QCOMPARE(t->rgTrackGain, -3.25);
        QCOMPARE(t->rgAlbumGain, -4.5);
        QVERIFY(std::abs(t->rgTrackPeak - 0.1) < 1e-6);
        QVERIFY(std::abs(t->rgAlbumPeak - 0.2) < 1e-6);
    }
};

QTEST_GUILESS_MAIN(TestDecoder)
#include "test_decoder.moc"
