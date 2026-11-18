#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QVariantMap>

namespace halo {

class Player;

// Linux desktop media controls (MPRIS 2): media keys, lock-screen and panel widgets.
class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ schemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimes)
public:
    explicit MprisRoot(QObject *parent);
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Halo Sync"); }
    QString desktopEntry() const { return QStringLiteral("halo-sync"); }
    QStringList schemes() const { return {QStringLiteral("file")}; }
    QStringList mimes() const { return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg")}; }
public slots:
    void Raise();
    void Quit();
signals:
    void raiseRequested();
};

class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ yes)
    Q_PROPERTY(bool CanGoPrevious READ yes)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanSeek READ yes)
    Q_PROPERTY(bool CanControl READ yes)
public:
    MprisPlayer(QObject *parent, Player *player);
    QString playbackStatus() const;
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    double rate() const { return 1.0; }
    bool yes() const { return true; }
public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong positionUs);
signals:
    void Seeked(qlonglong positionUs);
private:
    void notify(const QStringList &props);
    Player *m_player;
};

// Registers both adaptors on the session bus. Returns false when no bus is available.
bool registerMpris(QObject *owner, Player *player, QObject **rootOut);

} // namespace halo
