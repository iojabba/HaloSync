#include "Player.h"
#include "Paths.h"
#include <QRandomGenerator>
#include <QUrl>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace halo {

Player::Player(Database *db, QObject *parent) : QObject(parent), m_db(db)
{
    m_player.setAudioOutput(&m_output);
    auto s = Paths::settings();
    m_volume = s->value("player/volume", 0.8).toFloat();
    m_replayGain = s->value("player/replayGain", true).toBool();
    m_shuffle = s->value("player/shuffle", false).toBool();
    m_repeat = Repeat(s->value("player/repeat", 0).toInt());
    applyVolume();

    connect(&m_player, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
        emit positionChanged(p);
        maybeRecordPlay();
    });
    connect(&m_player, &QMediaPlayer::durationChanged, this, &Player::durationChanged);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st) {
        emit playingChanged(st == QMediaPlayer::PlayingState);
    });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
        if (st == QMediaPlayer::EndOfMedia) {
            if (!m_counted && m_index >= 0) { m_counted = true; m_db->recordPlay(m_queue[m_index].id); }
            const int n = nextIndex(false);
            if (n >= 0) load(n, true);
            else { m_player.stop(); emit playingChanged(false); }
        } else if (st == QMediaPlayer::InvalidMedia) {
            // SPEC §2.5: songs that can't be found are skipped.
            const QString p = m_index >= 0 ? m_queue[m_index].path : QString();
            logLine("player", "can't play " + p);
            emit error(QStringLiteral("Can't play %1").arg(QFileInfo(p).fileName()));
            const int n = nextIndex(false);
            if (n >= 0 && n != m_index) load(n, true);
        }
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg) {
        logLine("player", msg);
    });
}

std::optional<Track> Player::current() const
{
    if (m_index < 0 || m_index >= m_queue.size()) return std::nullopt;
    return m_queue[m_index];
}

bool Player::isPlaying() const { return m_player.playbackState() == QMediaPlayer::PlayingState; }
qint64 Player::position() const { return m_player.position(); }
qint64 Player::duration() const { return m_player.duration(); }

void Player::playTracks(const TrackList &tracks, int start)
{
    m_queue = tracks;
    m_order.resize(m_queue.size());
    std::iota(m_order.begin(), m_order.end(), 0);
    if (m_shuffle && !m_queue.isEmpty()) {
        std::mt19937 rng(QRandomGenerator::global()->generate());
        std::shuffle(m_order.begin(), m_order.end(), rng);
        // Start the order at the requested song.
        auto it = std::find(m_order.begin(), m_order.end(), qBound(0, start, int(m_queue.size()) - 1));
        std::iter_swap(m_order.begin(), it);
    }
    emit queueChanged();
    if (m_queue.isEmpty()) { stop(); m_index = -1; emit currentChanged(); return; }
    load(qBound(0, start, int(m_queue.size()) - 1), true);
}

void Player::enqueue(const TrackList &tracks)
{
    if (m_queue.isEmpty()) { playTracks(tracks); return; }
    const int base = int(m_queue.size());
    m_queue += tracks;
    for (int i = 0; i < tracks.size(); ++i) m_order.push_back(base + i);
    emit queueChanged();
}

void Player::playAt(int index) { if (index >= 0 && index < m_queue.size()) load(index, true); }

void Player::removeAt(int index)
{
    if (index < 0 || index >= m_queue.size()) return;
    const bool wasCurrent = index == m_index;
    m_queue.removeAt(index);
    m_order.removeAll(index);
    for (int &o : m_order) if (o > index) --o;
    if (index < m_index) --m_index;
    emit queueChanged();
    if (wasCurrent) {
        if (m_queue.isEmpty()) { stop(); m_index = -1; emit currentChanged(); }
        else load(qMin(index, int(m_queue.size()) - 1), isPlaying());
    }
}

void Player::clearQueue() { stop(); m_queue.clear(); m_order.clear(); m_index = -1; emit queueChanged(); emit currentChanged(); }

void Player::load(int index, bool autoplay)
{
    m_index = index;
    m_counted = false;
    m_player.setSource(QUrl::fromLocalFile(m_queue[index].path));
    applyVolume();
    emit currentChanged();
    if (autoplay) m_player.play();
}

void Player::togglePlayPause()
{
    if (isPlaying()) pause();
    else play();
}

void Player::play()
{
    if (m_index < 0 && !m_queue.isEmpty()) { load(m_order.value(0, 0), true); return; }
    m_player.play();
}

void Player::pause() { m_player.pause(); }
void Player::stop() { m_player.stop(); }

int Player::nextIndex(bool userAction) const
{
    if (m_queue.isEmpty()) return -1;
    if (m_repeat == RepeatOne && !userAction) return m_index;
    const int pos = int(m_order.indexOf(m_index));
    if (pos + 1 < m_order.size()) return m_order[pos + 1];
    return m_repeat == RepeatOff && !userAction ? -1 : m_order.value(0, -1);
}

void Player::next()
{
    const int n = nextIndex(true);
    if (n >= 0) load(n, true);
}

void Player::previous()
{
    if (m_player.position() > 3000 || m_queue.isEmpty()) { m_player.setPosition(0); return; }
    const int pos = int(m_order.indexOf(m_index));
    load(pos > 0 ? m_order[pos - 1] : m_order.value(m_order.size() - 1, 0), true);
}

void Player::seek(qint64 ms) { m_player.setPosition(ms); }

void Player::setVolume(float v)
{
    m_volume = qBound(0.0f, v, 1.0f);
    Paths::settings()->setValue("player/volume", m_volume);
    applyVolume();
}

void Player::setMuted(bool m) { m_output.setMuted(m); emit modesChanged(); }
void Player::toggleMute() { setMuted(!m_output.isMuted()); }

void Player::setShuffle(bool on)
{
    m_shuffle = on;
    Paths::settings()->setValue("player/shuffle", on);
    // Rebuild the order around the current song.
    m_order.resize(m_queue.size());
    std::iota(m_order.begin(), m_order.end(), 0);
    if (on && !m_queue.isEmpty()) {
        std::mt19937 rng(QRandomGenerator::global()->generate());
        std::shuffle(m_order.begin(), m_order.end(), rng);
        if (m_index >= 0) std::iter_swap(m_order.begin(), std::find(m_order.begin(), m_order.end(), m_index));
    }
    emit modesChanged();
}

void Player::cycleRepeat()
{
    m_repeat = Repeat((int(m_repeat) + 1) % 3);
    Paths::settings()->setValue("player/repeat", int(m_repeat));
    emit modesChanged();
}

void Player::setReplayGain(bool on)
{
    m_replayGain = on;
    Paths::settings()->setValue("player/replayGain", on);
    applyVolume();
}

void Player::applyVolume()
{
    float v = m_volume;
    if (m_replayGain && m_index >= 0 && m_index < m_queue.size()) {
        const Track &t = m_queue[m_index];
        const double gain = t.rgAlbumGain != 0 ? t.rgAlbumGain : t.rgTrackGain;
        if (gain != 0) v *= float(std::min(1.0, std::pow(10.0, gain / 20.0)));
    }
    m_output.setVolume(v);
}

void Player::maybeRecordPlay()
{
    if (m_counted || m_index < 0) return;
    const qint64 dur = m_player.duration();
    if (dur > 0 && m_player.position() >= dur / 2) {
        m_counted = true;
        m_db->recordPlay(m_queue[m_index].id);
    }
}

void Player::refreshCurrent()
{
    for (Track &t : m_queue)
        if (auto fresh = m_db->track(t.id)) t = *fresh;
    emit queueChanged();
    emit currentChanged();
}

} // namespace halo
