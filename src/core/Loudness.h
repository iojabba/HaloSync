#pragma once
#include <QString>
#include <atomic>
#include <vector>

namespace halo {

// ITU-R BS.1770-4 / EBU R128 loudness, used to compute ReplayGain 2.0 values
// (reference level -18 LUFS) for songs that have no ReplayGain tags.
struct LoudnessResult {
    bool ok = false;
    double lufs = -70;                 // integrated loudness
    double peak = 0;                   // sample peak, linear (1.0 = full scale)
    std::vector<double> blocks;        // gating-block mean squares, for album loudness
    QString error;
};

class Loudness {
public:
    static constexpr double ReferenceLufs = -18.0;

    static LoudnessResult measure(const QString &path, std::atomic_bool *cancel = nullptr);
    // Integrated loudness of a set of 400 ms gating blocks (absolute and relative gates).
    static double integrated(const std::vector<double> &blocks);
    static double gainDb(double lufs) { return ReferenceLufs - lufs; }
};

} // namespace halo
