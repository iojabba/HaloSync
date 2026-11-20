#pragma once
#include <QString>
#include <atomic>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVPacket;
struct AVFrame;

namespace halo {

// Decodes one file or stream URL to interleaved stereo float at 48 kHz using
// the FFmpeg libraries. Encoder delay and padding (LAME header, iTunSMPB, edit
// lists) are trimmed by FFmpeg, which is what makes gapless playback work.
// Used by the audio engine, the loudness scanner and internet radio.
class Decoder {
public:
    static constexpr int Rate = 48000;
    static constexpr int Channels = 2;

    Decoder();
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;

    // A local path or an http(s) URL. Network opens can be cancelled through
    // the interrupt flag.
    bool open(const QString &source, QString *error = nullptr, std::atomic_bool *interrupt = nullptr);
    void close();
    bool isOpen() const { return m_fmt != nullptr; }
    bool isStream() const { return m_stream; }

    // Reads up to `frames` frames into `out` (frames * 2 floats).
    // Returns the number of frames written; 0 means the end; -1 an error.
    int read(float *out, int frames);
    bool seek(qint64 ms);
    qint64 durationMs() const { return m_durationMs; }
    int sourceChannels() const { return m_sourceChannels; }

    // ICY "StreamTitle" for internet radio, empty if none. Cheap to poll.
    QString streamTitle() const;
    QString lastError() const { return m_error; }

private:
    bool fill();               // decode more into m_pending; false at end or error
    void convert(AVFrame *f);
    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_codec = nullptr;
    SwrContext *m_swr = nullptr;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_frame = nullptr;
    int m_streamIndex = -1;
    int m_sourceChannels = 2;
    bool m_stream = false;
    bool m_eof = false;          // demuxer finished
    bool m_flushed = false;      // decoder and resampler drained
    qint64 m_durationMs = 0;
    qint64 m_skipFrames = 0;     // frames to drop after a seek, for sample-accurate seeking
    qint64 m_lastPts = 0;        // pts of the last decoded frame (stream time base)
    std::vector<float> m_pending;
    size_t m_pendingPos = 0;
    QString m_error;
    std::atomic_bool *m_interrupt = nullptr;
};

} // namespace halo
