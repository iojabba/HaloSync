#include "Mpris.h"
#include "Player.h"
#include "TagIO.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>
#include <QUrl>

namespace halo {

MprisRoot::MprisRoot(QObject *parent) : QDBusAbstractAdaptor(parent) {}
void MprisRoot::Raise() { emit raiseRequested(); }
void MprisRoot::Quit() { QCoreApplication::quit(); }

MprisPlayer::MprisPlayer(QObject *parent, Player *player) : QDBusAbstractAdaptor(parent), m_player(player)
{
    connect(player, &Player::playingChanged, this, [this] { notify({"PlaybackStatus"}); });
    connect(player, &Player::currentChanged, this, [this] { notify({"Metadata", "PlaybackStatus"}); });
}

QString MprisPlayer::playbackStatus() const
{
    if (m_player->isPlaying()) return QStringLiteral("Playing");
    return m_player->current() ? QStringLiteral("Paused") : QStringLiteral("Stopped");
}

QVariantMap MprisPlayer::metadata() const
{
    QVariantMap m;
    const auto t = m_player->current();
    if (!t) {
        m["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
        return m;
    }
    m["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/halosync/track/%1").arg(t->id)));
    m["mpris:length"] = qlonglong(t->durationMs * 1000);
    m["xesam:title"] = t->displayTitle();
    m["xesam:artist"] = QStringList{t->displayArtist()};
    m["xesam:albumArtist"] = QStringList{t->displayAlbumArtist()};
    m["xesam:album"] = t->displayAlbum();
    m["xesam:url"] = QUrl::fromLocalFile(t->path).toString();
    if (t->trackNo) m["xesam:trackNumber"] = t->trackNo;
    const QString art = TagIO::folderArtPath(QFileInfo(t->path).absolutePath());
    if (!art.isEmpty()) m["mpris:artUrl"] = QUrl::fromLocalFile(art).toString();
    return m;
}

double MprisPlayer::volume() const { return m_player->volume(); }
void MprisPlayer::setVolume(double v) { m_player->setVolume(float(v)); }
qlonglong MprisPlayer::position() const { return m_player->position() * 1000; }

void MprisPlayer::Next() { m_player->next(); }
void MprisPlayer::Previous() { m_player->previous(); }
void MprisPlayer::Pause() { m_player->pause(); }
void MprisPlayer::PlayPause() { m_player->togglePlayPause(); }
void MprisPlayer::Stop() { m_player->stop(); }
void MprisPlayer::Play() { m_player->play(); }
void MprisPlayer::Seek(qlonglong offsetUs) { m_player->seek(qMax<qint64>(0, m_player->position() + offsetUs / 1000)); emit Seeked(position()); }
void MprisPlayer::SetPosition(const QDBusObjectPath &, qlonglong positionUs) { m_player->seek(positionUs / 1000); emit Seeked(position()); }

void MprisPlayer::notify(const QStringList &props)
{
    QVariantMap changed;
    for (const QString &p : props) {
        if (p == "PlaybackStatus") changed[p] = playbackStatus();
        else if (p == "Metadata") changed[p] = metadata();
    }
    QDBusMessage msg = QDBusMessage::createSignal("/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "PropertiesChanged");
    msg << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

bool registerMpris(QObject *owner, Player *player, QObject **rootOut)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    auto *holder = new QObject(owner);
    auto *root = new MprisRoot(holder);
    new MprisPlayer(holder, player);
    if (!bus.registerObject("/org/mpris/MediaPlayer2", holder) ||
        !bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.halosync"))) {
        delete holder;
        return false;
    }
    if (rootOut) *rootOut = root;
    return true;
}

} // namespace halo
