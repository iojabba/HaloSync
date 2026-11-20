#include "MainWindow.h"
#include "CollectionPage.h"
#include "Library.h"
#include "MiniPlayer.h"
#include "Paths.h"
#include "Player.h"
#include "QuickplayPage.h"
#include "SettingsPage.h"
#include "SyncPage.h"
#include "DeviceDetect.h"
#include "TagIO.h"
#include "Theme.h"
#include "TrackModel.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace halo {

MainWindow::MainWindow(Library *library, Player *player, QWidget *parent)
    : QMainWindow(parent), m_lib(library), m_player(player)
{
    setWindowTitle(QStringLiteral("Halo Sync"));
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(28, 14, 28, 0);
    lay->setSpacing(6);

    // Header: brand + pivots
    auto *brand = new QLabel(QStringLiteral("HALO SYNC"));
    brand->setObjectName("brand");
    lay->addWidget(brand);
    auto *pivotRow = new QHBoxLayout;
    m_pivots = new QButtonGroup(this);
    m_pageNames = {"quickplay", "collection", "sync", "settings"};
    for (int i = 0; i < m_pageNames.size(); ++i) {
        auto *b = new QPushButton(m_pageNames[i]);
        b->setObjectName("pivot");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        m_pivots->addButton(b, i);
        pivotRow->addWidget(b);
    }
    pivotRow->addStretch();
    lay->addLayout(pivotRow);

    m_stack = new QStackedWidget;
    m_collection = new CollectionPage(library, player);
    m_quickplay = new QuickplayPage(library, player, m_collection);
    m_sync = new SyncPage(library);
    m_settings = new SettingsPage(library, player);
    m_stack->addWidget(m_quickplay);
    m_stack->addWidget(m_collection);
    m_stack->addWidget(m_sync);
    m_stack->addWidget(m_settings);
    lay->addWidget(m_stack, 1);

    m_status = new QLabel;
    m_status->setObjectName("dim");
    lay->addWidget(m_status);
    lay->addWidget(buildTransport());

    connect(m_pivots, &QButtonGroup::idClicked, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_collection, &CollectionPage::statusMessage, this, &MainWindow::showStatus);
    connect(m_collection, &CollectionPage::syncNowRequested, this, [this](qint64 id) { m_sync->syncDevice(id); });
    connect(m_sync, &SyncPage::statusMessage, this, &MainWindow::showStatus);
    connect(m_sync, &SyncPage::zuneConnected, this, [this](const QString &model) {
        showStatus(QStringLiteral("%1 connected. Halo Sync can't sync with Zune players.").arg(model));
        QMessageBox box(QMessageBox::Information, QStringLiteral("%1 connected").arg(model),
                        QStringLiteral("Halo Sync recognised your %1, but it can't sync with it.").arg(model), QMessageBox::Ok, this);
        box.setInformativeText(DeviceDetect::zuneNotice());
        box.exec();
    });
    connect(m_settings, &SettingsPage::statusMessage, this, &MainWindow::showStatus);
    connect(m_settings, &SettingsPage::themeChanged, this, [this] {
        Theme::apply(*qApp);
        m_quickplay->refresh();
        refreshNowPlaying();
    });
    connect(m_quickplay, &QuickplayPage::openCollection, this, [this] { showPage("collection"); });
    connect(m_lib, &Library::scanStarted, this, [this] { showStatus(QStringLiteral("Looking for new media…")); });
    connect(m_lib, &Library::scanFinished, this, [this](int changed, int removed) {
        showStatus(changed || removed ? QStringLiteral("Collection updated: %1 added or changed, %2 removed").arg(changed).arg(removed)
                                      : QStringLiteral("Collection is up to date"));
        m_quickplay->refresh();
    });
    connect(m_player, &Player::error, this, &MainWindow::showStatus);
    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(7000);
    connect(&m_statusTimer, &QTimer::timeout, m_status, &QLabel::clear);

    setupShortcuts();
    auto s = Paths::settings();
    showPage(s->value("ui/startPage", "quickplay").toString());
    if (!restoreGeometry(s->value("ui/geometry").toByteArray())) resize(1280, 820);
    setMinimumSize(900, 600);
    refreshNowPlaying();
    refreshModes();
}

QWidget *MainWindow::buildTransport()
{
    auto *bar = new QFrame;
    bar->setObjectName("transportBar");
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(0, 10, 0, 14);
    h->setSpacing(10);
    m_art = new QLabel;
    m_art->setFixedSize(52, 52);
    m_art->setScaledContents(true);
    h->addWidget(m_art);
    auto *text = new QVBoxLayout;
    text->setSpacing(0);
    m_title = new QLabel;
    m_title->setObjectName("nowTitle");
    m_sub = new QLabel;
    m_sub->setObjectName("nowSub");
    m_title->setMinimumWidth(200);
    m_title->setMaximumWidth(340);
    m_sub->setMaximumWidth(340);
    text->addWidget(m_title);
    text->addWidget(m_sub);
    h->addLayout(text);
    auto mk = [&](const QString &glyph, const QString &tip, bool checkable = false) {
        auto *b = new QPushButton(glyph);
        b->setObjectName("transport");
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        h->addWidget(b);
        return b;
    };
    m_like = mk("♥", "Like this song (Ctrl+L)", true);
    auto *prev = mk("⏮", "Previous (Ctrl+B)");
    m_play = mk("▶", "Play/Pause (Ctrl+P)");
    auto *next = mk("⏭", "Next (Ctrl+F)");
    auto *seekBox = new QVBoxLayout;
    m_seek = new QSlider(Qt::Horizontal);
    m_seek->setRange(0, 0);
    m_time = new QLabel(QStringLiteral("0:00 / 0:00"));
    m_time->setObjectName("dim");
    seekBox->addWidget(m_seek);
    seekBox->addWidget(m_time, 0, Qt::AlignRight);
    h->addLayout(seekBox, 1);
    m_shuffle = mk("⤮", "Shuffle (Ctrl+H)", true);
    m_repeat = mk("↻", "Repeat (Ctrl+T)", true);
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 100);
    m_volume->setFixedWidth(90);
    m_volume->setValue(int(m_player->volume() * 100));
    m_volume->setToolTip(QStringLiteral("Volume (F8 / F9)"));
    h->addWidget(m_volume);
    auto *mini = mk("⤡", "Mini player");

    connect(prev, &QPushButton::clicked, m_player, &Player::previous);
    connect(next, &QPushButton::clicked, m_player, &Player::next);
    connect(m_play, &QPushButton::clicked, m_player, &Player::togglePlayPause);
    connect(m_shuffle, &QPushButton::clicked, this, [this] { m_player->setShuffle(!m_player->shuffle()); });
    connect(m_repeat, &QPushButton::clicked, m_player, &Player::cycleRepeat);
    connect(mini, &QPushButton::clicked, this, &MainWindow::enterMiniMode);
    connect(m_like, &QPushButton::clicked, this, [this] {
        if (auto t = m_player->current()) {
            m_lib->db()->setRating(t->id, t->rating == Like ? Unrated : Like);
            m_player->refreshCurrent();
        }
    });
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) { m_player->setVolume(v / 100.0f); });
    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this] { m_seeking = false; m_player->seek(m_seek->value()); });
    connect(m_player, &Player::positionChanged, this, [this](qint64 p) {
        if (!m_seeking) m_seek->setValue(int(p));
        m_time->setText(TrackModel::formatDuration(p) + " / " + TrackModel::formatDuration(m_player->duration()));
    });
    connect(m_player, &Player::durationChanged, this, [this](qint64 d) { m_seek->setRange(0, int(d)); });
    connect(m_player, &Player::currentChanged, this, &MainWindow::refreshNowPlaying);
    connect(m_player, &Player::playingChanged, this, [this](bool playing) {
        m_play->setText(playing ? QStringLiteral("⏸") : QStringLiteral("▶"));
    });
    connect(m_player, &Player::modesChanged, this, &MainWindow::refreshModes);
    return bar;
}

void MainWindow::refreshNowPlaying()
{
    const auto t = m_player->current();
    const QColor accent = palette().color(QPalette::Highlight);
    QPixmap pm(104, 104);
    QString art;
    if (t) art = TagIO::folderArtPath(QFileInfo(t->path).absolutePath());
    QImage img;
    if (!art.isEmpty() && img.load(art)) pm = QPixmap::fromImage(img.scaled(104, 104, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    else pm.fill(accent);
    m_art->setPixmap(pm);
    const QFontMetrics fm(m_title->font());
    m_title->setText(t ? fm.elidedText(t->displayTitle().toLower(), Qt::ElideRight, 330) : QStringLiteral("nothing playing"));
    m_sub->setText(t ? QStringLiteral("%1 · %2").arg(t->displayArtist(), t->displayAlbum()) : QStringLiteral("double-click a song to play it"));
    m_like->setChecked(t && t->rating == Like);
    setWindowTitle(t ? QStringLiteral("%1 – %2 · Halo Sync").arg(t->displayTitle(), t->displayArtist()) : QStringLiteral("Halo Sync"));
}

void MainWindow::refreshModes()
{
    m_shuffle->setChecked(m_player->shuffle());
    m_repeat->setChecked(m_player->repeat() != Player::RepeatOff);
    m_repeat->setText(m_player->repeat() == Player::RepeatOne ? QStringLiteral("↻1") : QStringLiteral("↻"));
    m_repeat->setToolTip(m_player->repeat() == Player::RepeatOff ? QStringLiteral("Repeat is off (Ctrl+T)")
                         : m_player->repeat() == Player::RepeatAll ? QStringLiteral("Repeat all (Ctrl+T)")
                                                                   : QStringLiteral("Repeat one song (Ctrl+T)"));
}

void MainWindow::setupShortcuts()
{
    auto add = [this](const QKeySequence &k, auto fn) {
        auto *s = new QShortcut(k, this);
        s->setContext(Qt::ApplicationShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    add(QKeySequence(Qt::CTRL | Qt::Key_P), [this] { m_player->togglePlayPause(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_F), [this] { m_player->next(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_B), [this] { m_player->previous(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_S), [this] { m_player->stop(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_H), [this] { m_player->setShuffle(!m_player->shuffle()); });
    add(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { m_player->cycleRepeat(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_L), [this] { m_like->click(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_E), [this] { showPage("collection"); m_collection->focusSearch(); });
    add(QKeySequence(Qt::Key_F7), [this] { m_player->toggleMute(); });
    add(QKeySequence(Qt::Key_F8), [this] { m_volume->setValue(m_volume->value() - 5); });
    add(QKeySequence(Qt::Key_F9), [this] { m_volume->setValue(m_volume->value() + 5); });
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), [this] { m_player->seek(m_player->position() + 10000); });
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), [this] { m_player->seek(qMax<qint64>(0, m_player->position() - 10000)); });
    add(QKeySequence(Qt::CTRL | Qt::Key_M), [this] { enterMiniMode(); });
    // Keyboard media keys (when the window has focus; MPRIS covers the rest on Linux).
    add(QKeySequence(Qt::Key_MediaTogglePlayPause), [this] { m_player->togglePlayPause(); });
    add(QKeySequence(Qt::Key_MediaPlay), [this] { m_player->togglePlayPause(); });
    add(QKeySequence(Qt::Key_MediaPause), [this] { m_player->pause(); });
    add(QKeySequence(Qt::Key_MediaNext), [this] { m_player->next(); });
    add(QKeySequence(Qt::Key_MediaPrevious), [this] { m_player->previous(); });
    add(QKeySequence(Qt::Key_MediaStop), [this] { m_player->stop(); });
    for (int i = 0; i < m_pageNames.size(); ++i) {
        const QString name = m_pageNames[i];
        add(QKeySequence(Qt::ALT | Qt::Key(Qt::Key_1 + i)), [this, name] { showPage(name); });
    }
}

void MainWindow::showPage(const QString &name)
{
    const int i = qMax(0, int(m_pageNames.indexOf(name)));
    m_pivots->button(i)->setChecked(true);
    m_stack->setCurrentIndex(i);
}

void MainWindow::showStatus(const QString &text)
{
    m_status->setText(text);
    m_statusTimer.start();
}

void MainWindow::enterMiniMode()
{
    if (!m_mini) {
        m_mini = new MiniPlayer(m_player);
        connect(m_mini, &MiniPlayer::restoreRequested, this, &MainWindow::leaveMiniMode);
    }
    m_mini->show();
    hide();
}

void MainWindow::leaveMiniMode()
{
    if (m_mini) m_mini->hide();
    show();
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_sync->isSyncing() &&
        QMessageBox::question(this, QStringLiteral("Sync in progress"),
                              QStringLiteral("A sync is still running. Close anyway? Files copied so far are kept."))
            != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    Paths::settings()->setValue("ui/geometry", saveGeometry());
    if (m_mini) { m_mini->deleteLater(); m_mini = nullptr; }
    e->accept();
    qApp->quit();
}

} // namespace halo
