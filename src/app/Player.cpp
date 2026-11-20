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
    auto s = Paths::settings();
    m_volume = s->value("player/volume", 0.8).toFloat();
    m_shuffle = s->value("player/shuffle", false).toBool();
    m_repeat = Repeat(s->value("player/repeat", 0).toInt());
    // 0.1 stored a boolean; keep honouring it when no mode is saved yet.
    const bool legacyOn = s->value("player/replayGain", true).toBool();
    m_gainMode = GainMode(s->value("player/gainMode", legacyOn ? int(GainAlbum) : int(GainOff)).toInt());
    m_preampDb = s->value("player/preampDb", 0.0).toDouble();
    applyVolume();

    connect(&m_engine, &AudioEngine::positionChanged, this, [this](qint64 p) {
        emit positionChanged(p);
        maybeRecordPlay();
    });
    connect(&m_engine, &AudioEngine::durationChanged, this, &Player::durationChanged);
    connect(&m_engine, &AudioEngine::stateChanged, this, [this](AudioEngine::State st) {
        emit playingChanged(st == AudioEngine::Playing);
    });
    connect(&m_engine, &AudioEngine::trackStarted, this, &Player::onTrackStarted);
    connect(&m_engine, &AudioEngine::finished, this, [this] {
        if (!m_counted && m_index >= 0 && !m_stream) { m_counted = true; m_db->recordPlay(m_queue[m_index].id); }
    });
    connect(&m_engine, &AudioEngine::failed, this, [this](qint64 token, const QString &msg) {
        // SPEC §2.5: songs that can't be found are skipped.
        logLine("player", msg);
        if (token == m_curToken) {
            const QString p = m_index >= 0 ? m_queue[m_index].path : QString();
            emit error(m_stream ? QStringLiteral("Can't play this station") : QStringLiteral("Can't play %1").arg(QFileInfo(p).fileName()));
            const int n = m_stream ? -1 : nextIndex(false);
            if (n >= 0 && n != m_index) load(n, true);
            else m_engine.stop();
        } else if (token == m_nextToken && m_nextIdx >= 0) {
            // The queued next song can't be opened: skip past it.
            const int skip = m_nextIdx;
            const int pos = int(m_order.indexOf(skip));
            m_nextIdx = pos + 1 < m_order.size() ? m_order[pos + 1] : -1;
            m_nextToken = -1;
            if (m_nextIdx >= 0) {
                m_nextToken = ++m_tokenSeq;
                m_engine.setNext(m_queue[m_nextIdx].path, gainFor(m_queue[m_nextIdx], m_gainMode, m_preampDb), m_nextToken);
            }
        }
    });
    connect(&m_engine, &AudioEngine::streamTitleChanged, this, [this](const QString &title) {
        if (!m_stream || m_queue.isEmpty()) return;
        m_streamTitle = title;
        m_queue[0].artist = title;
        emit streamTitleChanged(title);
        emit currentChanged();
    });
}

void Player::onTrackStarted(qint64 token)
{
    if (token == m_curToken) return;                 // the song load() asked for
    if (token != m_nextToken || m_nextIdx < 0) return;
    // Gapless advance into the queued next song.
    if (!m_counted && m_index >= 0 && !m_stream) m_db->recordPlay(m_queue[m_index].id);
    m_index = m_nextIdx;
    m_curToken = token;
    m_counted = false;
    emit currentChanged();
    queueNext();
}

std::optional<Track> Player::current() const
{
    if (m_index < 0 || m_index >= m_queue.size()) return std::nullopt;
    return m_queue[m_index];
}

bool Player::isPlaying() const { return m_engine.state() == AudioEngine::Playing; }
qint64 Player::position() const { return m_engine.position(); }
qint64 Player::duration() const { return m_engine.duration(); }

void Player::playTracks(const TrackList &tracks, int start)
{
    m_stream = false;
    m_streamTitle.clear();
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
    if (m_queue.isEmpty() || m_stream) { playTracks(tracks); return; }
    const int base = int(m_queue.size());
    m_queue += tracks;
    for (int i = 0; i < tracks.size(); ++i) m_order.push_back(base + i);
    emit queueChanged();
    queueNext();
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
    if (m_nextIdx > index) --m_nextIdx;
    else if (m_nextIdx == index) m_nextIdx = -2;   // force queueNext() to re-pick
    emit queueChanged();
    if (wasCurrent) {
        if (m_queue.isEmpty()) { stop(); m_index = -1; emit currentChanged(); }
        else load(qMin(index, int(m_queue.size()) - 1), isPlaying());
    } else {
        queueNext();
    }
}

void Player::clearQueue()
{
    stop();
    m_queue.clear(); m_order.clear(); m_index = -1; m_nextIdx = -1; m_stream = false;
    m_engine.clearNext();
    emit queueChanged(); emit currentChanged();
}

void Player::load(int index, bool autoplay, qint64 startMs)
{
    m_index = index;
    m_counted = false;
    m_curToken = ++m_tokenSeq;
    m_engine.play(m_queue[index].path, currentGain(), m_curToken, startMs);
    if (!autoplay) m_engine.pause();
    emit currentChanged();
    queueNext();
}

void Player::queueNext()
{
    if (m_stream || m_index < 0) { m_nextIdx = -1; m_nextToken = -1; m_engine.clearNext(); return; }
    const int n = nextIndex(false);
    if (n == m_nextIdx && m_nextToken >= 0) return;   // unchanged
    m_nextIdx = n;
    if (n < 0) { m_nextToken = -1; m_engine.clearNext(); return; }
    m_nextToken = ++m_tokenSeq;
    m_engine.setNext(m_queue[n].path, gainFor(m_queue[n], m_gainMode, m_preampDb), m_nextToken);
}

void Player::playStream(const QString &name, const QString &url)
{
    Track t;
    t.title = name.isEmpty() ? url : name;
    t.path = url;
    t.format = QStringLiteral("stream");
    m_stream = true;
    m_streamTitle.clear();
    m_queue = {t};
    m_order = {0};
    m_index = 0;
    m_counted = true;           // radio doesn't count as song plays
    m_nextIdx = -1; m_nextToken = -1;
    m_engine.clearNext();
    m_curToken = ++m_tokenSeq;
    m_engine.play(url, 1.0f, m_curToken);
    emit queueChanged();
    emit currentChanged();
}

void Player::togglePlayPause()
{
    if (isPlaying()) pause();
    else play();
}

void Player::play()
{
    if (m_index < 0 && !m_queue.isEmpty()) { load(m_order.value(0, 0), true); return; }
    if (m_engine.state() == AudioEngine::Paused) { m_engine.resume(); return; }
    if (m_engine.state() == AudioEngine::Stopped && m_index >= 0) load(m_index, true);
}

void Player::pause() { if (m_stream) m_engine.stop(); else m_engine.pause(); }
void Player::stop() { m_engine.stop(); }

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
    if (m_stream) return;
    if (m_engine.position() > 3000 || m_queue.isEmpty()) { m_engine.seek(0); return; }
    const int pos = int(m_order.indexOf(m_index));
    load(pos > 0 ? m_order[pos - 1] : m_order.value(m_order.size() - 1, 0), true);
}

void Player::seek(qint64 ms) { m_engine.seek(ms); }

void Player::setVolume(float v)
{
    m_volume = qBound(0.0f, v, 1.0f);
    Paths::settings()->setValue("player/volume", m_volume);
    applyVolume();
}

void Player::setMuted(bool m) { m_engine.setMuted(m); emit modesChanged(); }
void Player::toggleMute() { setMuted(!m_engine.isMuted()); }

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
    queueNext();
}

void Player::cycleRepeat()
{
    m_repeat = Repeat((int(m_repeat) + 1) % 3);
    Paths::settings()->setValue("player/repeat", int(m_repeat));
    emit modesChanged();
    queueNext();
}

void Player::setReplayGain(bool on) { setGainMode(on ? GainAlbum : GainOff); }

void Player::setGainMode(GainMode mode)
{
    m_gainMode = mode;
    Paths::settings()->setValue("player/gainMode", int(mode));
    Paths::settings()->setValue("player/replayGain", mode != GainOff);
    applyGain();
}

void Player::setPreampDb(double db)
{
    m_preampDb = std::clamp(db, -15.0, 15.0);
    Paths::settings()->setValue("player/preampDb", m_preampDb);
    applyGain();
}

float Player::gainFor(const Track &t, GainMode mode, double preampDb)
{
    if (mode == GainOff || t.format == QLatin1String("stream")) return 1.0f;
    const bool hasTrack = t.rgTrackGain != 0 || t.rgTrackPeak > 0;
    const bool hasAlbum = t.rgAlbumGain != 0 || t.rgAlbumPeak > 0;
    if (!hasTrack && !hasAlbum) return 1.0f;      // untagged songs play as they are
    const bool album = (mode == GainAlbum && hasAlbum) || !hasTrack;
    const double gainDb = (album ? t.rgAlbumGain : t.rgTrackGain) + preampDb;
    const double peak = album ? t.rgAlbumPeak : t.rgTrackPeak;
    double scale = std::pow(10.0, gainDb / 20.0);
    if (peak > 0) scale = std::min(scale, 1.0 / peak);   // clipping protection
    return float(std::clamp(scale, 0.01, 8.0));
}

float Player::currentGain() const
{
    if (m_index < 0 || m_index >= m_queue.size()) return 1.0f;
    return gainFor(m_queue[m_index], m_gainMode, m_preampDb);
}

void Player::applyVolume() { m_engine.setVolume(m_volume); }

void Player::applyGain()
{
    m_engine.setCurrentGain(currentGain());
    if (m_nextIdx >= 0) {             // re-queue so the next song picks up the new gain
        m_nextIdx = -1;
        m_nextToken = -1;
        queueNext();
    }
}

void Player::maybeRecordPlay()
{
    if (m_counted || m_index < 0) return;
    const qint64 dur = m_engine.duration();
    if (dur > 0 && m_engine.position() >= dur / 2) {
        m_counted = true;
        m_db->recordPlay(m_queue[m_index].id);
    }
}

void Player::refreshCurrent()
{
    if (m_stream) return;
    for (Track &t : m_queue)
        if (auto fresh = m_db->track(t.id)) t = *fresh;
    applyGain();
    emit queueChanged();
    emit currentChanged();
}

} // namespace halo
