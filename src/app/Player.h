#pragma once
#include "Database.h"
#include <QObject>
#include "AudioEngine.h"
#include <QHash>

namespace halo {

// Now Playing queue + transport (SPEC §2.5, §2.7). Playback goes through the
// gapless AudioEngine; the next song is always queued in the engine so it
// starts with no gap. ReplayGain is applied to the samples (track or album
// mode, pre-amp, clipping protection). Internet radio streams play through
// the same engine as a one-item queue.
class Player : public QObject {
    Q_OBJECT
public:
    enum Repeat { RepeatOff, RepeatAll, RepeatOne };
    explicit Player(Database *db, QObject *parent = nullptr);

    const TrackList &queue() const { return m_queue; }
    int currentIndex() const { return m_index; }
    std::optional<Track> current() const;
    bool isPlaying() const;
    bool shuffle() const { return m_shuffle; }
    Repeat repeat() const { return m_repeat; }
    qint64 position() const;
    qint64 duration() const;
    float volume() const { return m_volume; }
    bool isMuted() const { return m_engine.isMuted(); }
    bool isStream() const { return m_stream; }
    QString streamTitle() const { return m_streamTitle; }

    enum GainMode { GainOff, GainTrack, GainAlbum };
    GainMode gainMode() const { return m_gainMode; }
    double preampDb() const { return m_preampDb; }
    // Linear sample scale for a song under the current ReplayGain settings.
    static float gainFor(const Track &t, GainMode mode, double preampDb);

public slots:
    void playTracks(const halo::TrackList &tracks, int start = 0);
    void enqueue(const halo::TrackList &tracks);
    void playAt(int index);
    void removeAt(int index);
    void clearQueue();
    void togglePlayPause();
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seek(qint64 ms);
    void setVolume(float v);       // 0..1
    void setMuted(bool m);
    void toggleMute();
    void setShuffle(bool on);
    void cycleRepeat();
    void setReplayGain(bool on);   // on = album mode (the default), off = no leveling
    void setGainMode(halo::Player::GainMode mode);
    void setPreampDb(double db);
    void playStream(const QString &name, const QString &url);
    void refreshCurrent();         // after tag/rating edits

signals:
    void queueChanged();
    void currentChanged();         // track changed (or none)
    void playingChanged(bool playing);
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void modesChanged();
    void error(const QString &message);
    void streamTitleChanged(const QString &title);

private:
    void load(int index, bool autoplay, qint64 startMs = 0);
    void applyVolume();
    void applyGain();
    void queueNext();              // tell the engine which song follows
    int  nextIndex(bool userAction) const;
    void maybeRecordPlay();
    void onTrackStarted(qint64 token);
    float currentGain() const;
    Database *m_db;
    AudioEngine m_engine;
    qint64 m_tokenSeq = 0;
    qint64 m_curToken = -1, m_nextToken = -1;
    int m_nextIdx = -1;
    bool m_stream = false;
    QString m_streamTitle;
    GainMode m_gainMode = GainAlbum;
    double m_preampDb = 0;
    TrackList m_queue;
    QVector<int> m_order;          // play order (shuffled or identity)
    int m_index = -1;              // index into m_queue
    bool m_shuffle = false;
    Repeat m_repeat = RepeatOff;
    float m_volume = 0.8f;
    bool m_counted = false;
};

} // namespace halo
