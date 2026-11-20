#pragma once
#include "Database.h"
#include <QStringList>
#include <atomic>
#include <functional>

namespace halo {

// Calculates ReplayGain 2.0 track and album values (BS.1770 loudness,
// -18 LUFS reference) and writes them into the files' tags (SPEC §5, 2b).
struct ReplayGainReport {
    int measured = 0;
    int written = 0;
    int failed = 0;
    QStringList errors;
};

namespace ReplayGainJob {
    bool hasGain(const Track &t);
    // Songs from `tracks` that need analysis: all, or only those without tags.
    TrackList pending(const TrackList &tracks, bool onlyMissing);
    // Blocking; analyses in parallel. Album values come from all songs of an
    // album (same album artist and album) that are in `tracks`.
    ReplayGainReport run(Database *db, const TrackList &tracks,
                         const std::function<void(int done, int total)> &progress = {},
                         std::atomic_bool *cancel = nullptr);
}

} // namespace halo
