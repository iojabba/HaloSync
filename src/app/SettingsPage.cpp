#include "SettingsPage.h"
#include "Database.h"
#include "ReplayGainJob.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>
#include "Library.h"
#include "Paths.h"
#include "Player.h"
#include "Theme.h"
#include "TrackModel.h"
#include "Transcoder.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace halo {

namespace {
QLabel *section(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("h2");
    l->setContentsMargins(0, 18, 0, 4);
    return l;
}
QLabel *note(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("dim");
    l->setWordWrap(true);
    return l;
}
} // namespace

SettingsPage::SettingsPage(Library *library, Player *player, QWidget *parent)
    : QWidget(parent), m_lib(library), m_player(player)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *body = new QWidget;
    scroll->setWidget(body);
    auto *l = new QVBoxLayout(body);
    l->setContentsMargins(0, 0, 24, 24);
    auto s = Paths::settings();

    // Collection
    l->addWidget(section("collection: monitored folders"));
    l->addWidget(note("Halo Sync watches these folders and adds new music automatically. "
                      "Removing a folder takes its songs out of the collection but never deletes files."));
    m_folders = new QListWidget;
    m_folders->setMaximumHeight(150);
    l->addWidget(m_folders);
    auto *fr = new QHBoxLayout;
    auto *addF = new QPushButton("add folder");
    auto *remF = new QPushButton("remove");
    auto *rescan = new QPushButton("rescan now");
    fr->addWidget(addF);
    fr->addWidget(remF);
    fr->addWidget(rescan);
    fr->addStretch();
    l->addLayout(fr);
    auto *del = new QCheckBox("Delete files from the computer when I remove them from the collection");
    del->setChecked(s->value("library/deleteFiles", false).toBool());
    l->addWidget(del);

    // Playback
    l->addWidget(section("playback"));
    auto *rgRow = new QHBoxLayout;
    rgRow->addWidget(new QLabel("Volume leveling (ReplayGain):"));
    auto *rgMode = new QComboBox;
    rgMode->addItems({"off", "by song", "by album"});
    rgMode->setToolTip("By album keeps the loudness differences between songs of one album, "
                       "and falls back to the song value when there's no album value.");
    rgMode->setCurrentIndex(int(m_player->gainMode()));
    rgRow->addWidget(rgMode);
    rgRow->addSpacing(16);
    rgRow->addWidget(new QLabel("Pre-amp:"));
    auto *preamp = new QDoubleSpinBox;
    preamp->setRange(-12, 12);
    preamp->setSingleStep(0.5);
    preamp->setSuffix(" dB");
    preamp->setValue(m_player->preampDb());
    rgRow->addWidget(preamp);
    rgRow->addStretch();
    l->addLayout(rgRow);
    l->addWidget(note("Songs are never made loud enough to clip. Songs without ReplayGain information play "
                      "unchanged; Halo Sync can measure them (EBU R128) and save the result in their tags."));
    auto *rgScanRow = new QHBoxLayout;
    auto *rgScan = new QPushButton("measure songs without ReplayGain");
    auto *rgStatus = new QLabel;
    rgStatus->setObjectName("dim");
    rgScanRow->addWidget(rgScan);
    rgScanRow->addWidget(rgStatus);
    rgScanRow->addStretch();
    l->addLayout(rgScanRow);
    auto *startRow = new QHBoxLayout;
    startRow->addWidget(new QLabel("Show this page when Halo Sync opens:"));
    auto *startup = new QComboBox;
    startup->addItems({"quickplay", "collection", "sync"});
    startup->setCurrentText(s->value("ui/startPage", "quickplay").toString());
    startRow->addWidget(startup);
    startRow->addStretch();
    l->addLayout(startRow);

    // Look
    l->addWidget(section("display"));
    auto *themeRow = new QHBoxLayout;
    auto *darkBtn = new QPushButton("dark");
    auto *lightBtn = new QPushButton("light");
    darkBtn->setCheckable(true);
    lightBtn->setCheckable(true);
    auto *grp = new QButtonGroup(this);
    grp->addButton(darkBtn);
    grp->addButton(lightBtn);
    (Theme::dark() ? darkBtn : lightBtn)->setChecked(true);
    themeRow->addWidget(darkBtn);
    themeRow->addWidget(lightBtn);
    themeRow->addSpacing(24);
    themeRow->addWidget(new QLabel("Accent:"));
    for (const auto &p : Theme::accentPresets()) {
        auto *b = new QPushButton;
        b->setFixedSize(28, 28);
        b->setToolTip(p.name);
        b->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: none; }").arg(p.color.name()));
        const QColor c = p.color;
        connect(b, &QPushButton::clicked, this, [this, c] {
            Paths::settings()->setValue("ui/accent", c.name());
            emit themeChanged();
        });
        themeRow->addWidget(b);
    }
    auto *custom = new QPushButton("custom…");
    themeRow->addWidget(custom);
    themeRow->addStretch();
    l->addLayout(themeRow);
    connect(darkBtn, &QPushButton::clicked, this, [this] { Paths::settings()->setValue("ui/dark", true); emit themeChanged(); });
    connect(lightBtn, &QPushButton::clicked, this, [this] { Paths::settings()->setValue("ui/dark", false); emit themeChanged(); });
    connect(custom, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(Theme::accent(), this, "Accent colour");
        if (!c.isValid()) return;
        Paths::settings()->setValue("ui/accent", c.name());
        emit themeChanged();
    });

    // Conversion
    l->addWidget(section("conversion (sync)"));
    m_ffmpegLabel = note({});
    l->addWidget(m_ffmpegLabel);
    auto *ffRow = new QHBoxLayout;
    auto *ffPath = new QLineEdit(s->value("sync/ffmpeg").toString());
    ffPath->setPlaceholderText("ffmpeg path (leave empty to find it automatically)");
    auto *ffBrowse = new QPushButton("browse");
    ffRow->addWidget(ffPath, 1);
    ffRow->addWidget(ffBrowse);
    l->addLayout(ffRow);
    auto *cacheRow = new QHBoxLayout;
    cacheRow->addWidget(new QLabel("Keep converted files for reuse, up to"));
    auto *cacheMax = new QSpinBox;
    cacheMax->setRange(0, 200000);
    cacheMax->setSingleStep(256);
    cacheMax->setSuffix(" MB");
    cacheMax->setValue(s->value("sync/cacheMaxMB", 2048).toInt());
    cacheRow->addWidget(cacheMax);
    auto *clear = new QPushButton("clear cache");
    cacheRow->addWidget(clear);
    cacheRow->addStretch();
    l->addLayout(cacheRow);
    m_cacheLabel = note({});
    l->addWidget(m_cacheLabel);

    // About / data
    l->addWidget(section("data and logs"));
    l->addWidget(note(QStringLiteral("Library, settings and log are kept in %1%2.")
                          .arg(QDir::toNativeSeparators(Paths::dataDir()),
                               Paths::portable() ? " (portable mode)" : "")));
    l->addWidget(note("Portable mode: put an empty file named \"portable\" next to the program and "
                      "everything is stored in a data folder beside it."));
    auto *logs = new QPushButton("open data folder");
    l->addWidget(logs, 0, Qt::AlignLeft);
    l->addWidget(note("Halo Sync is an independent, clean-room media manager. It doesn't use or contain any "
                      "Microsoft Zune code, and it can't sync with Zune players."));
    l->addStretch();

    connect(addF, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Select a folder to add",
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
        if (dir.isEmpty()) return;
        const QString err = m_lib->addFolder(dir);
        if (!err.isEmpty()) QMessageBox::warning(this, "Can't add", err);
        refreshFolders();
    });
    connect(remF, &QPushButton::clicked, this, [this] {
        QListWidgetItem *it = m_folders->currentItem();
        if (!it) return;
        if (QMessageBox::question(this, "Remove monitored folder",
                QStringLiteral("Halo Sync will stop watching \"%1\" and its songs leave the collection. "
                               "No files or folders are deleted.").arg(it->text())) != QMessageBox::Yes) return;
        m_lib->removeFolder(it->data(Qt::UserRole).toLongLong());
        refreshFolders();
    });
    connect(rescan, &QPushButton::clicked, m_lib, &Library::rescan);
    connect(del, &QCheckBox::toggled, this, [](bool on) { Paths::settings()->setValue("library/deleteFiles", on); });
    connect(rgMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int i) { m_player->setGainMode(Player::GainMode(i)); });
    connect(preamp, qOverload<double>(&QDoubleSpinBox::valueChanged), m_player, &Player::setPreampDb);
    connect(rgScan, &QPushButton::clicked, this, [this, rgScan, rgStatus] {
        if (m_rgCancel) { *m_rgCancel = true; return; }      // second click cancels
        const TrackList todo = ReplayGainJob::pending(m_lib->db()->allTracks(), true);
        if (todo.isEmpty()) { rgStatus->setText("Every song already has ReplayGain information."); return; }
        m_rgCancel = std::make_shared<std::atomic_bool>(false);
        rgScan->setText("cancel");
        auto cancel = m_rgCancel;
        QPointer<QLabel> status(rgStatus);
        auto *watcher = new QFutureWatcher<ReplayGainReport>(this);
        connect(watcher, &QFutureWatcher<ReplayGainReport>::finished, this, [this, watcher, rgScan, rgStatus] {
            const ReplayGainReport r = watcher->result();
            watcher->deleteLater();
            m_rgCancel.reset();
            rgScan->setText("measure songs without ReplayGain");
            rgStatus->setText(r.errors.contains("cancelled") ? QStringLiteral("Cancelled.")
                              : QStringLiteral("Measured %1 songs, %2 couldn't be updated.").arg(r.written).arg(r.failed));
            emit m_lib->db()->tracksChanged();
            m_player->refreshCurrent();
        });
        // The job writes to the database from a worker thread, so it uses its own connection.
        const QString dbFile = Paths::databaseFile();
        watcher->setFuture(QtConcurrent::run([todo, cancel, status, dbFile] {
            Database local;
            if (!local.open(dbFile)) return ReplayGainReport{0, 0, int(todo.size()), {local.lastError()}};
            return ReplayGainJob::run(&local, todo, [status](int done, int total) {
                QMetaObject::invokeMethod(qApp, [status, done, total] {
                    if (status) status->setText(QStringLiteral("Measuring %1 of %2…").arg(done).arg(total));
                });
            }, cancel.get());
        }));
    });
    connect(startup, &QComboBox::currentTextChanged, this, [](const QString &t) { Paths::settings()->setValue("ui/startPage", t); });
    connect(ffPath, &QLineEdit::editingFinished, this, [this, ffPath] {
        Paths::settings()->setValue("sync/ffmpeg", ffPath->text().trimmed());
        refreshCache();
    });
    connect(ffBrowse, &QPushButton::clicked, this, [this, ffPath] {
        const QString f = QFileDialog::getOpenFileName(this, "Find ffmpeg");
        if (f.isEmpty()) return;
        ffPath->setText(f);
        Paths::settings()->setValue("sync/ffmpeg", f);
        refreshCache();
    });
    connect(cacheMax, &QSpinBox::valueChanged, this, [](int v) { Paths::settings()->setValue("sync/cacheMaxMB", v); });
    connect(clear, &QPushButton::clicked, this, [this] { Transcoder::clearCache(); refreshCache(); });
    connect(logs, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::dataDir())); });
    connect(m_lib->db(), &Database::tracksChanged, this, &SettingsPage::refreshFolders);
    refreshFolders();
    refreshCache();
}

void SettingsPage::refreshFolders()
{
    m_folders->clear();
    for (const MonitoredFolder &f : m_lib->db()->folders()) {
        auto *it = new QListWidgetItem(QDir::toNativeSeparators(f.path));
        it->setData(Qt::UserRole, f.id);
        if (!QFileInfo(f.path).isDir()) it->setText(it->text() + "   (not connected)");
        m_folders->addItem(it);
    }
}

void SettingsPage::refreshCache()
{
    const QString ff = Transcoder::ffmpegPath();
    m_ffmpegLabel->setText(ff.isEmpty()
        ? "ffmpeg wasn't found. It's needed to convert FLAC, Ogg, Opus and other formats a device can't play."
        : QStringLiteral("Converting with %1").arg(QDir::toNativeSeparators(ff)));
    m_cacheLabel->setText(QStringLiteral("Cache in use: %1").arg(TrackModel::formatSize(Transcoder::cacheSize())));
}

} // namespace halo
