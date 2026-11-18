#pragma once
#include "Database.h"
#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>

namespace halo {

// Now Playing queue + transport (SPEC §2.5, §2.7). ReplayGain is applied as a
// volume scale. Qt Multimedia has no gapless mode, so tracks change with a
// short gap.
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
    void setReplayGain(bool on);
    void refreshCurrent();         // after tag/rating edits

signals:
    void queueChanged();
    void currentChanged();         // track changed (or none)
    void playingChanged(bool playing);
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void modesChanged();
    void error(const QString &message);

private:
    void load(int index, bool autoplay);
    void applyVolume();
    int  nextIndex(bool userAction) const;
    void maybeRecordPlay();
    Database *m_db;
    QMediaPlayer m_player;
    QAudioOutput m_output;
    TrackList m_queue;
    QVector<int> m_order;          // play order (shuffled or identity)
    int m_index = -1;              // index into m_queue
    bool m_shuffle = false;
    Repeat m_repeat = RepeatOff;
    float m_volume = 0.8f;
    bool m_replayGain = true;
    bool m_counted = false;
};

} // namespace halo
