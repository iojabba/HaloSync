#pragma once
#include <QIODevice>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

class QAudioSink;

namespace halo {

class Decoder;

// Gapless playback engine (SPEC §5 Phase 2b). A worker thread decodes with
// FFmpeg into a ring buffer; a QAudioSink pulls from it. When a song ends the
// worker carries straight on with the song given to setNext(), so there is no
// gap. Each song has a caller-chosen token; trackStarted(token) fires when
// that song becomes audible.
class AudioEngine : public QObject {
    Q_OBJECT
public:
    enum State { Stopped, Playing, Paused };
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    // gain is a linear scale applied to the samples (ReplayGain), 1 = none.
    void play(const QString &source, float gain, qint64 token, qint64 startMs = 0);
    void setNext(const QString &source, float gain, qint64 token);   // empty source = none
    void clearNext() { setNext({}, 1.0f, -1); }
    void pause();
    void resume();
    void stop();
    void seek(qint64 ms);
    void setVolume(float v);       // 0..1, output volume
    void setMuted(bool m);
    void setCurrentGain(float gain);

    State state() const { return m_state; }
    bool isMuted() const { return m_muted; }
    qint64 position() const;       // ms into the audible song
    qint64 duration() const { return m_durationMs; }
    qint64 currentToken() const { return m_token; }

signals:
    void trackStarted(qint64 token);         // a song became audible (play() or gapless advance)
    void durationChanged(qint64 ms);
    void positionChanged(qint64 ms);
    void stateChanged(halo::AudioEngine::State state);
    void finished();                         // the last song ended and there was no next
    void failed(qint64 token, const QString &message);
    void streamTitleChanged(const QString &title);   // ICY now-playing for radio

private:
    struct Boundary {
        qint64 frame;            // ring frame where this song (or seek target) starts
        qint64 token;
        qint64 durationMs;
        qint64 offsetMs;         // song position at that frame
        QString source;
        float gain;
        bool seek;               // a seek within the same song, not a new song
    };
    class Source;
    class Worker;
    friend class Source;
    friend class Worker;

    void startSink();
    void poll();
    qint64 playedFrames() const;

    // Shared with the worker and the sink's pull device; guarded by m_mutex.
    mutable QMutex m_mutex;
    QWaitCondition m_wake;
    std::vector<float> m_ring;     // interleaved stereo
    qint64 m_written = 0;          // frames written into the ring, total
    qint64 m_read = 0;             // frames handed to the sink, total
    std::deque<Boundary> m_bounds; // song starts not yet reached by m_read
    struct Segment { qint64 delivered; qint64 read; qint64 frames; };
    std::deque<Segment> m_segments; // real audio within the sink's stream (the rest is silence)
    qint64 m_delivered = 0;        // frames given to the sink since it started, silence included
    qint64 m_sinkBaseRead = 0;     // m_read when the sink started
    bool m_endQueued = false;      // the worker reached the end with nothing next
    qint64 m_endFrame = 0;
    QString m_cmdSource, m_nextSource;
    float m_cmdGain = 1, m_nextGain = 1, m_gain = 1;
    qint64 m_cmdToken = -1, m_nextToken = -1, m_cmdStartMs = 0;
    bool m_cmdOpen = false, m_cmdStop = false, m_cmdIsSeek = false, m_nextConsumed = false;
    bool m_rewind = false, m_rearm = false, m_currentGainChanged = false;
    QString m_streamTitle;
    std::atomic_bool m_interrupt{false};
    std::atomic_bool m_quit{false};

    std::unique_ptr<Worker> m_worker;
    std::unique_ptr<Source> m_source;
    QAudioSink *m_sink = nullptr;
    QTimer m_timer;
    State m_state = Stopped;
    bool m_floatOut = true;
    bool m_muted = false;
    float m_volume = 0.8f;
    qint64 m_token = -1;
    qint64 m_trackStartFrame = 0;  // ring frame where the audible song starts
    qint64 m_trackOffsetMs = 0;    // song position at that frame (after a seek)
    qint64 m_durationMs = 0;
    QString m_lastTitle;
    QString m_currentSource;
    float m_currentGain = 1;
    bool m_seekPending = false;    // seek issued, new audio not audible yet
    bool m_seekRequest = false;
};

} // namespace halo

Q_DECLARE_METATYPE(halo::AudioEngine::State)
