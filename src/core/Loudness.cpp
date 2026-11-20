#include "Loudness.h"
#include "Decoder.h"
#include <cmath>

namespace halo {

namespace {
// K-weighting filter at 48 kHz (BS.1770-4, table 1 and 2).
struct Biquad {
    double b0, b1, b2, a1, a2;
    double z1 = 0, z2 = 0;
    double run(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
Biquad shelf() { return {1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585}; }
Biquad highpass() { return {1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621}; }
double toLufs(double meanSquare) { return -0.691 + 10.0 * std::log10(std::max(meanSquare, 1e-20)); }
} // namespace

double Loudness::integrated(const std::vector<double> &blocks)
{
    // Absolute gate at -70 LUFS, then relative gate 10 LU below the gated mean.
    double sum = 0;
    size_t n = 0;
    for (double z : blocks) if (toLufs(z) > -70.0) { sum += z; ++n; }
    if (n == 0) return -70.0;
    const double relGate = toLufs(sum / double(n)) - 10.0;
    sum = 0; n = 0;
    for (double z : blocks) if (toLufs(z) > -70.0 && toLufs(z) > relGate) { sum += z; ++n; }
    return n ? toLufs(sum / double(n)) : -70.0;
}

LoudnessResult Loudness::measure(const QString &path, std::atomic_bool *cancel)
{
    LoudnessResult res;
    Decoder dec;
    if (!dec.open(path, &res.error)) return res;

    constexpr int Seg = Decoder::Rate / 10;     // 100 ms
    Biquad f1[2] = {shelf(), shelf()};
    Biquad f2[2] = {highpass(), highpass()};
    std::vector<double> segments;               // sum of squares per 100 ms segment
    std::vector<float> buf(size_t(Seg) * Decoder::Channels);
    double acc = 0;
    int accFrames = 0;
    // A mono file is decoded to two identical channels; BS.1770 counts it once.
    const int measured = dec.sourceChannels() == 1 ? 1 : 2;
    for (;;) {
        if (cancel && cancel->load()) { res.error = QStringLiteral("cancelled"); return res; }
        const int n = dec.read(buf.data(), Seg);
        if (n < 0) { res.error = dec.lastError(); return res; }
        if (n == 0) break;
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < measured; ++c) {
                const double x = buf[size_t(i) * 2 + c];
                res.peak = std::max(res.peak, std::abs(x));
                const double y = f2[c].run(f1[c].run(x));
                acc += y * y;
            }
            if (++accFrames == Seg) { segments.push_back(acc); acc = 0; accFrames = 0; }
        }
    }
    // 400 ms blocks with 75 % overlap = four consecutive 100 ms segments.
    for (size_t i = 0; i + 4 <= segments.size(); ++i)
        res.blocks.push_back((segments[i] + segments[i + 1] + segments[i + 2] + segments[i + 3]) / (4.0 * Seg));
    if (res.blocks.empty() && !segments.empty()) {   // shorter than 400 ms: one partial block
        double s = 0;
        for (double v : segments) s += v;
        res.blocks.push_back(s / (double(segments.size()) * Seg));
    }
    res.lufs = integrated(res.blocks);
    res.ok = true;
    return res;
}

} // namespace halo
