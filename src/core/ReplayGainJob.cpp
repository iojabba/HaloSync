#include "ReplayGainJob.h"
#include "Loudness.h"
#include "Paths.h"
#include "TagIO.h"
#include <QHash>
#include <QMutex>
#include <QtConcurrent>
#include <algorithm>

namespace halo {

bool ReplayGainJob::hasGain(const Track &t) { return t.rgTrackGain != 0 || t.rgTrackPeak > 0; }

TrackList ReplayGainJob::pending(const TrackList &tracks, bool onlyMissing)
{
    if (!onlyMissing) return tracks;
    // An album is analysed as a whole, so include every song of an album where any is missing.
    QSet<QString> albums;
    auto key = [](const Track &t) { return t.album.isEmpty() ? QString() : t.displayAlbumArtist().toLower() + '\x1f' + t.album.toLower(); };
    for (const Track &t : tracks) if (!hasGain(t)) albums.insert(key(t));
    TrackList out;
    for (const Track &t : tracks) {
        const QString k = key(t);
        if (!hasGain(t) || (!k.isEmpty() && albums.contains(k))) out << t;
    }
    return out;
}

ReplayGainReport ReplayGainJob::run(Database *db, const TrackList &tracks,
                                    const std::function<void(int, int)> &progress, std::atomic_bool *cancel)
{
    ReplayGainReport rep;
    const int total = int(tracks.size());
    std::atomic_int done{0};
    QVector<LoudnessResult> results = QtConcurrent::blockingMapped(tracks, [&](const Track &t) {
        LoudnessResult r = (cancel && cancel->load()) ? LoudnessResult{} : Loudness::measure(t.path, cancel);
        const int d = ++done;
        if (progress) progress(d, total);
        return r;
    });
    if (cancel && cancel->load()) { rep.errors << QStringLiteral("cancelled"); return rep; }

    // Album loudness from the gating blocks of all songs in the album.
    QHash<QString, QVector<int>> albums;
    for (int i = 0; i < total; ++i)
        if (results[i].ok && !tracks[i].album.isEmpty())
            albums[tracks[i].displayAlbumArtist().toLower() + '\x1f' + tracks[i].album.toLower()] << i;
    QVector<double> albumGain(total, 0), albumPeak(total, 0);
    for (const QVector<int> &idx : albums) {
        if (idx.size() < 2) continue;      // a lone song: album value = track value is misleading, leave it out
        std::vector<double> blocks;
        double peak = 0;
        for (int i : idx) {
            blocks.insert(blocks.end(), results[i].blocks.begin(), results[i].blocks.end());
            peak = std::max(peak, results[i].peak);
        }
        const double gain = Loudness::gainDb(Loudness::integrated(blocks));
        for (int i : idx) { albumGain[i] = gain; albumPeak[i] = peak; }
    }

    db->begin();
    for (int i = 0; i < total; ++i) {
        const Track &t = tracks[i];
        if (!results[i].ok) {
            ++rep.failed;
            rep.errors << QStringLiteral("%1: %2").arg(t.path, results[i].error);
            continue;
        }
        ++rep.measured;
        QString err;
        double tg = Loudness::gainDb(results[i].lufs);
        if (tg == 0) tg = 0.001;           // 0 means "no gain" in the library
        if (!TagIO::writeReplayGain(t.path, tg, results[i].peak, albumGain[i], albumPeak[i], &err)) {
            ++rep.failed;
            rep.errors << err;
            continue;
        }
        ++rep.written;
        if (auto fresh = TagIO::read(t.path)) db->upsertTrack(*fresh);
    }
    db->commit();
    logLine("replaygain", QStringLiteral("measured %1, written %2, failed %3").arg(rep.measured).arg(rep.written).arg(rep.failed));
    return rep;
}

} // namespace halo
