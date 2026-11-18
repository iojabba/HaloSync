#pragma once
#include <QMainWindow>
#include <QTimer>

class QButtonGroup;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

class QCloseEvent;

namespace halo {

class CollectionPage;
class Library;
class MiniPlayer;
class Player;
class QuickplayPage;
class SettingsPage;
class SyncPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Library *library, Player *player, QWidget *parent = nullptr);
    void showPage(const QString &name);       // quickplay | collection | sync | settings
    CollectionPage *collection() const { return m_collection; }

public slots:
    void showStatus(const QString &text);

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    QWidget *buildTransport();
    void refreshNowPlaying();
    void refreshModes();
    void setupShortcuts();
    void enterMiniMode();
    void leaveMiniMode();

    Library *m_lib;
    Player *m_player;
    QButtonGroup *m_pivots;
    QStackedWidget *m_stack;
    QStringList m_pageNames;
    QuickplayPage *m_quickplay;
    CollectionPage *m_collection;
    SyncPage *m_sync;
    SettingsPage *m_settings;
    MiniPlayer *m_mini = nullptr;
    QLabel *m_status;
    QTimer m_statusTimer;
    // transport
    QLabel *m_art, *m_title, *m_sub, *m_time;
    QSlider *m_seek, *m_volume;
    QPushButton *m_play, *m_shuffle, *m_repeat, *m_like;
    bool m_seeking = false;
};

} // namespace halo
