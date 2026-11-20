#include "AudioEngine.h"
#include "Decoder.h"
#include "Paths.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <algorithm>
#include <cstring>

namespace halo {

namespace {
constexpr qint64 RingFrames = Decoder::Rate * 4;   // 4 s of audio
constexpr int ChunkFrames = 2048;
}

// ------------------------------------------------------------------ pull device
class AudioEngine::Source : public QIODevice {
public:
    explicit Source(AudioEngine *e) : m_e(e) { open(QIODevice::ReadOnly); }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 20; }
protected:
    qint64 writeData(const char *, qint64) override { return -1; }
    qint64 readData(char *data, qint64 maxlen) override
    {
        const int bpf = m_e->m_floatOut ? 8 : 4;
        const qint64 want = maxlen / bpf;
        qint64 n = 0;
        {
            QMutexLocker lock(&m_e->m_mutex);
            n = std::min(want, m_e->m_written - m_e->m_read);
            if (n > 0) {
                auto &segs = m_e->m_segments;
                if (!segs.empty() && segs.back().delivered + segs.back().frames == m_e->m_delivered &&
                    segs.back().read + segs.back().frames == m_e->m_read)
                    segs.back().frames += n;          // contiguous with the previous segment
                else
                    segs.push_back({m_e->m_delivered, m_e->m_read, n});
                if (segs.size() > 256) segs.pop_front();
            }
            m_e->m_delivered += want;
            for (qint64 i = 0; i < n; ++i) {
                const float *src = m_e->m_ring.data() + ((m_e->m_read + i) % RingFrames) * 2;
                if (m_e->m_floatOut) {
                    std::memcpy(data + i * 8, src, 8);
                } else {
                    auto *dst = reinterpret_cast<qint16 *>(data + i * 4);
                    dst[0] = qint16(std::clamp(src[0], -1.0f, 1.0f) * 32767.0f);
                    dst[1] = qint16(std::clamp(src[1], -1.0f, 1.0f) * 32767.0f);
                }
            }
            m_e->m_read += n;
        }
        if (n > 0) m_e->m_wake.wakeAll();
        // Underrun or end: pad with silence so the sink keeps pulling.
        std::memset(data + n * bpf, 0, size_t((want - n) * bpf));
        return want * bpf;
    }
private:
    AudioEngine *m_e;
};

// ------------------------------------------------------------------ decoder thread
class AudioEngine::Worker : public QThread {
public:
    explicit Worker(AudioEngine *e) : m_e(e) {}
protected:
    void run() override
    {
        Decoder dec;
        bool haveDec = false, atEnd = false;
        float gain = 1;
        int titleTick = 0;
        std::vector<float> chunk(size_t(ChunkFrames) * 2);
        AudioEngine *e = m_e;
        auto fail = [e](qint64 token, const QString &msg) {
            logLine("audio", msg);
            QMetaObject::invokeMethod(e, [e, token, msg] { emit e->failed(token, msg); }, Qt::QueuedConnection);
        };

        QMutexLocker lock(&e->m_mutex);
        // Opens `src` with the lock released; returns false if a newer command arrived meanwhile.
        auto openUnlocked = [&](const QString &src, qint64 startMs, bool *ok, QString *err) {
            e->m_interrupt = false;
            lock.unlock();
            *ok = dec.open(src, err, &e->m_interrupt);
            if (*ok && startMs > 0) dec.seek(startMs);
            lock.relock();
            return !(e->m_cmdOpen || e->m_cmdStop);
        };
        // Moves on to the queued next song (gapless) or marks the end.
        auto advance = [&]() {
            haveDec = false;
            dec.close();
            if (!e->m_nextSource.isEmpty() && !e->m_nextConsumed) {
                const QString src = e->m_nextSource;
                const qint64 token = e->m_nextToken;
                const float g = e->m_nextGain;
                e->m_nextConsumed = true;
                bool ok = false;
                QString err;
                if (!openUnlocked(src, 0, &ok, &err)) return;
                if (ok) {
                    haveDec = true; atEnd = false; gain = g;
                    e->m_bounds.push_back({e->m_written, token, dec.durationMs(), 0, src, g, false});
                    return;
                }
                fail(token, QStringLiteral("Can't play %1: %2").arg(src, err));
            }
            atEnd = true;
            e->m_endQueued = true;
            e->m_endFrame = e->m_written;
        };

        while (!e->m_quit) {
            if (e->m_cmdStop) {
                e->m_cmdStop = false;
                dec.close();
                haveDec = false; atEnd = false;
                continue;
            }
            if (e->m_cmdOpen) {
                e->m_cmdOpen = false;
                const QString src = e->m_cmdSource;
                const qint64 token = e->m_cmdToken, startMs = e->m_cmdStartMs;
                const float g = e->m_cmdGain;
                const bool isSeek = e->m_cmdIsSeek;
                haveDec = false; atEnd = false;
                dec.close();
                bool ok = false;
                QString err;
                if (!openUnlocked(src, startMs, &ok, &err)) continue;
                if (!ok) { fail(token, QStringLiteral("Can't play %1: %2").arg(src, err)); continue; }
                haveDec = true; gain = g;
                e->m_bounds.push_back({e->m_written, token, dec.durationMs(), startMs, src, g, isSeek});
                continue;
            }
            if (e->m_rewind) {         // the next song changed after we had started decoding it
                e->m_rewind = false;
                advance();
                continue;
            }
            if (e->m_rearm) {          // a next song arrived after we had reached the end
                e->m_rearm = false;
                if (atEnd) { atEnd = false; e->m_endQueued = false; advance(); }
                continue;
            }
            if (haveDec && !atEnd && RingFrames - (e->m_written - e->m_read) >= ChunkFrames) {
                if (e->m_currentGainChanged) { e->m_currentGainChanged = false; gain = e->m_gain; }
                lock.unlock();
                const int n = dec.read(chunk.data(), ChunkFrames);
                QString title;
                const bool pollTitle = dec.isStream() && ++titleTick % 40 == 0;
                if (pollTitle) title = dec.streamTitle();
                const QString err = n < 0 ? dec.lastError() : QString();
                lock.relock();
                if (e->m_cmdOpen || e->m_cmdStop || e->m_rewind) continue;
                if (pollTitle) e->m_streamTitle = title;
                if (n > 0) {
                    for (int i = 0; i < n; ++i) {
                        float *dst = e->m_ring.data() + ((e->m_written + i) % RingFrames) * 2;
                        dst[0] = chunk[size_t(i) * 2] * gain;
                        dst[1] = chunk[size_t(i) * 2 + 1] * gain;
                    }
                    e->m_written += n;
                } else {
                    if (n < 0) logLine("audio", "decode error: " + err);
                    advance();
                }
                continue;
            }
            e->m_wake.wait(&e->m_mutex, 20);
        }
    }
private:
    AudioEngine *m_e;
};

// ------------------------------------------------------------------ engine
AudioEngine::AudioEngine(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<halo::AudioEngine::State>();
    m_ring.assign(size_t(RingFrames) * 2, 0.0f);
    m_source = std::make_unique<Source>(this);
    m_worker = std::make_unique<Worker>(this);
    m_worker->start();
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &AudioEngine::poll);
    m_timer.start();
}

AudioEngine::~AudioEngine()
{
    if (m_sink) m_sink->stop();
    m_quit = true;
    m_interrupt = true;
    m_wake.wakeAll();
    m_worker->wait();
}

void AudioEngine::startSink()
{
    if (!m_sink) {
        const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        QAudioFormat fmt;
        fmt.setSampleRate(Decoder::Rate);
        fmt.setChannelCount(2);
        fmt.setSampleFormat(QAudioFormat::Float);
        if (!dev.isFormatSupported(fmt)) {
            fmt.setSampleFormat(QAudioFormat::Int16);
            m_floatOut = false;
        }
        m_sink = new QAudioSink(dev, fmt, this);
        m_sink->setBufferSize(Decoder::Rate * (m_floatOut ? 8 : 4) / 5);   // 200 ms
        m_sink->setVolume(m_muted ? 0.0 : m_volume);
    }
    m_sink->stop();
    {
        QMutexLocker lock(&m_mutex);
        m_delivered = 0;
        m_segments.clear();
        m_sinkBaseRead = m_read;
    }
    m_sink->start(m_source.get());
}

void AudioEngine::play(const QString &source, float gain, qint64 token, qint64 startMs)
{
    {
        QMutexLocker lock(&m_mutex);
        m_cmdOpen = true;
        m_cmdSource = source;
        m_cmdGain = gain;
        m_cmdToken = token;
        m_cmdStartMs = startMs;
        m_cmdIsSeek = m_seekRequest;
        m_interrupt = true;       // abandon any open still in progress
        m_written = m_read;       // drop what is buffered
        m_bounds.clear();
        m_endQueued = false;
        m_nextConsumed = false;
        m_rewind = m_rearm = false;
        m_streamTitle.clear();
    }
    m_wake.wakeAll();
    startSink();
    if (m_state != Playing) { m_state = Playing; emit stateChanged(m_state); }
}

void AudioEngine::setNext(const QString &source, float gain, qint64 token)
{
    {
        QMutexLocker lock(&m_mutex);
        if (!m_nextConsumed && source == m_nextSource && token == m_nextToken) return;
        const qint64 oldToken = m_nextToken;
        m_nextSource = source;
        m_nextGain = gain;
        m_nextToken = token;
        if (m_nextConsumed) {
            // Already decoding the old next song. If it isn't audible yet, cut it off.
            auto it = std::find_if(m_bounds.begin(), m_bounds.end(),
                                   [&](const Boundary &b) { return b.token == oldToken && b.frame >= m_read; });
            if (it != m_bounds.end()) {
                m_written = it->frame;
                m_bounds.erase(it, m_bounds.end());
                m_nextConsumed = false;
                m_endQueued = false;
                m_rewind = true;
            } else {
                m_nextConsumed = false;   // the old one is playing; this is the one after it
            }
        }
        // The worker already hit the end of everything it had: carry on with this one.
        if (m_endQueued && !m_rewind && !source.isEmpty()) m_rearm = true;
    }
    m_wake.wakeAll();
}

void AudioEngine::pause()
{
    if (m_state != Playing || !m_sink) return;
    m_sink->suspend();
    m_state = Paused;
    emit stateChanged(m_state);
}

void AudioEngine::resume()
{
    if (m_state != Paused || !m_sink) return;
    m_sink->resume();
    m_state = Playing;
    emit stateChanged(m_state);
}

void AudioEngine::stop()
{
    {
        QMutexLocker lock(&m_mutex);
        m_cmdStop = true;
        m_cmdOpen = false;
        m_interrupt = true;
        m_written = m_read;
        m_bounds.clear();
        m_endQueued = false;
        m_nextConsumed = false;
    }
    m_wake.wakeAll();
    if (m_sink) m_sink->stop();
    m_trackOffsetMs = 0;
    m_trackStartFrame = m_read;
    if (m_state != Stopped) { m_state = Stopped; emit stateChanged(m_state); }
    emit positionChanged(0);
}

void AudioEngine::seek(qint64 ms)
{
    if (m_currentSource.isEmpty() || m_durationMs <= 0) return;   // streams can't seek
    const State before = m_state;
    ms = std::clamp<qint64>(ms, 0, m_durationMs);
    // Re-queue the next song too: play() forgets it was already consumed.
    m_seekRequest = true;
    play(m_currentSource, m_currentGain, m_token, ms);
    m_seekRequest = false;
    m_seekPending = true;
    m_trackOffsetMs = ms;
    if (before == Paused) pause();
}

void AudioEngine::setVolume(float v)
{
    m_volume = std::clamp(v, 0.0f, 1.0f);
    if (m_sink) m_sink->setVolume(m_muted ? 0.0 : m_volume);
}

void AudioEngine::setMuted(bool m)
{
    m_muted = m;
    if (m_sink) m_sink->setVolume(m_muted ? 0.0 : m_volume);
}

void AudioEngine::setCurrentGain(float gain)
{
    QMutexLocker lock(&m_mutex);
    m_gain = gain;
    m_currentGain = gain;
    m_currentGainChanged = true;
}

qint64 AudioEngine::playedFrames() const
{
    // processedUSecs counts everything the sink has consumed, silence included.
    // Map it back to ring frames through the segments of real audio we delivered.
    const qint64 processed = m_sink && m_state != Stopped ? m_sink->processedUSecs() * Decoder::Rate / 1000000 : 0;
    QMutexLocker lock(&m_mutex);
    if (!m_sink || m_state == Stopped) return m_read;
    qint64 played = m_sinkBaseRead;
    for (auto it = m_segments.rbegin(); it != m_segments.rend(); ++it) {
        if (it->delivered <= processed) {
            played = it->read + std::min(it->frames, processed - it->delivered);
            break;
        }
    }
    return played;
}

qint64 AudioEngine::position() const
{
    if (m_seekPending) return m_trackOffsetMs;
    const qint64 frames = std::max<qint64>(0, playedFrames() - m_trackStartFrame);
    return m_trackOffsetMs + frames * 1000 / Decoder::Rate;
}

void AudioEngine::poll()
{
    if (m_state == Stopped) return;
    const qint64 played = playedFrames();
    QVector<Boundary> started;
    bool ended = false;
    QString title;
    {
        QMutexLocker lock(&m_mutex);
        while (!m_bounds.empty() && m_bounds.front().frame <= played) {
            started.push_back(m_bounds.front());
            m_bounds.pop_front();
        }
        if (m_endQueued && !m_rearm && m_bounds.empty() && played >= m_endFrame && !m_cmdOpen) {
            m_endQueued = false;
            ended = true;
        }
        title = m_streamTitle;
    }
    for (const Boundary &b : started) {
        m_seekPending = false;
        m_trackStartFrame = b.frame;
        m_trackOffsetMs = b.offsetMs;
        m_currentSource = b.source;
        m_currentGain = b.gain;
        m_token = b.token;
        if (b.durationMs != m_durationMs) { m_durationMs = b.durationMs; emit durationChanged(m_durationMs); }
        if (!b.seek) emit trackStarted(b.token);
    }
    if (title != m_lastTitle) { m_lastTitle = title; emit streamTitleChanged(title); }
    if (m_state == Playing) emit positionChanged(position());
    if (ended) {
        if (m_sink) m_sink->stop();
        m_state = Stopped;
        emit stateChanged(m_state);
        emit finished();
    }
}

} // namespace halo
