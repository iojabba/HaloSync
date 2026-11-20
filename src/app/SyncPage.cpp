#include "SyncPage.h"
#include "DeviceDetect.h"
#include "Library.h"
#include "Paths.h"
#include "TrackModel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace halo {

static bool dark();

// ------------------------------------------------------------------ storage bar
StorageBar::StorageBar(QWidget *parent) : QWidget(parent) { setMinimumHeight(46); }

void StorageBar::setBreakdown(const StorageBreakdown &b) { m_b = b; update(); }

void StorageBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect bar(0, 0, width(), 14);
    const QColor track = palette().color(QPalette::Mid);
    p.fillRect(bar, track.darker(dark() ? 250 : 100));
    if (!m_b.valid || m_b.total <= 0) return;
    const QColor accent = palette().color(QPalette::Highlight);
    struct Seg { qint64 v; QColor c; const char *name; };
    const Seg segs[] = {{m_b.music, accent, "music"}, {m_b.video, accent.lighter(140), "videos"},
                        {m_b.pictures, QColor(0x3A, 0xA0, 0xE0), "pictures"}, {m_b.podcasts, QColor(0x8C, 0xBF, 0x26), "podcasts"},
                        {m_b.other, QColor(0x88, 0x88, 0x88), "other"}};
    int x = 0;
    QFont f = font();
    f.setPointSizeF(8.5);
    p.setFont(f);
    int lx = 0;
    for (const Seg &s : segs) {
        const int w = int(double(s.v) / double(m_b.total) * width());
        if (w > 0) p.fillRect(QRect(x, 0, w, 14), s.c);
        x += w;
        if (s.v <= 0) continue;
        p.fillRect(QRect(lx, 24, 10, 10), s.c);
        const QString label = QStringLiteral("%1 %2").arg(s.name, TrackModel::formatSize(s.v));
        p.setPen(palette().color(QPalette::WindowText));
        p.drawText(lx + 14, 33, label);
        lx += 22 + p.fontMetrics().horizontalAdvance(label);
    }
    const QString free = QStringLiteral("free %1").arg(TrackModel::formatSize(m_b.free));
    p.drawText(lx, 33, free);
}

static bool dark() { return Paths::settings()->value("ui/dark", true).toBool(); }

// ------------------------------------------------------------------ page
SyncPage::SyncPage(Library *library, QWidget *parent) : QWidget(parent), m_lib(library), m_db(library->db())
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_zune = new QLabel;
    m_zune->setObjectName("notice");
    m_zune->setWordWrap(true);
    m_zune->hide();
    outer->addWidget(m_zune);

    auto *split = new QSplitter;
    outer->addWidget(split, 1);

    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    auto *h = new QLabel(QStringLiteral("DEVICES"));
    h->setObjectName("h2");
    ll->addWidget(h);
    m_devices = new QListWidget;
    ll->addWidget(m_devices, 1);
    auto *add = new QPushButton(QStringLiteral("add device"));
    ll->addWidget(add);
    auto *hint = new QLabel(QStringLiteral("A device is any drive or folder: a USB player, an SD card, "
                                           "a phone in USB storage mode, or a network folder."));
    hint->setObjectName("dim");
    hint->setWordWrap(true);
    ll->addWidget(hint);
    split->addWidget(left);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_detail = new QWidget;
    scroll->setWidget(m_detail);
    auto *dl = new QVBoxLayout(m_detail);
    dl->setContentsMargins(16, 0, 8, 0);
    m_name = new QLineEdit;
    m_name->setObjectName("h1");
    m_name->setStyleSheet("QLineEdit { border: none; font-size: 30px; font-weight: 300; background: transparent; }");
    dl->addWidget(m_name);
    m_storage = new StorageBar;
    dl->addWidget(m_storage);
    m_storageText = new QLabel;
    m_storageText->setObjectName("dim");
    dl->addWidget(m_storageText);
    auto *runRow = new QHBoxLayout;
    m_preview = new QPushButton(QStringLiteral("preview"));
    m_sync = new QPushButton(QStringLiteral("start sync"));
    m_sync->setObjectName("accent");
    m_cancel = new QPushButton(QStringLiteral("cancel"));
    m_cancel->setEnabled(false);
    runRow->addWidget(m_preview);
    runRow->addWidget(m_sync);
    runRow->addWidget(m_cancel);
    runRow->addStretch();
    dl->addLayout(runRow);
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    m_progress->hide();
    dl->addWidget(m_progress);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setObjectName("dim");
    dl->addWidget(m_status);

    auto *form = new QFormLayout;
    auto *pathRow = new QHBoxLayout;
    m_path = new QLineEdit;
    auto *browse = new QPushButton(QStringLiteral("change"));
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browse);
    form->addRow(QStringLiteral("Location"), pathRow);
    m_mode = new QComboBox;
    m_mode->addItem(QStringLiteral("All: sync my whole collection"), "all");
    m_mode->addItem(QStringLiteral("Items I choose: sync only what I add"), "chosen");
    m_mode->addItem(QStringLiteral("Manual: copy what I send, never remove"), "manual");
    form->addRow(QStringLiteral("Sync"), m_mode);
    m_formats = new QLineEdit;
    m_formats->setPlaceholderText(QStringLiteral("mp3, m4a, aac, wma"));
    m_formats->setToolTip(QStringLiteral("File types the device plays. Everything else is converted."));
    form->addRow(QStringLiteral("Device plays"), m_formats);
    auto *convRow = new QHBoxLayout;
    m_codec = new QComboBox;
    for (const QString &c : TranscodeTarget::codecs()) m_codec->addItem(c.toUpper(), c);
    m_bitrate = new QComboBox;
    for (int b : {96, 128, 160, 192, 256, 320}) m_bitrate->addItem(QStringLiteral("%1 Kbps").arg(b), b);
    convRow->addWidget(m_codec);
    convRow->addWidget(m_bitrate);
    convRow->addStretch();
    form->addRow(QStringLiteral("Convert others to"), convRow);
    m_threshold = new QSpinBox;
    m_threshold->setRange(0, 2000);
    m_threshold->setSingleStep(32);
    m_threshold->setSuffix(QStringLiteral(" Kbps"));
    m_threshold->setSpecialValueText(QStringLiteral("off"));
    m_threshold->setToolTip(QStringLiteral("Also convert supported songs above this bit rate (saves space)."));
    form->addRow(QStringLiteral("Convert above"), m_threshold);
    m_skipDisliked = new QCheckBox(QStringLiteral("Don't sync songs rated Don't like"));
    m_writePlaylists = new QCheckBox(QStringLiteral("Copy playlists to the device (.m3u)"));
    form->addRow(QString(), m_skipDisliked);
    form->addRow(QString(), m_writePlaylists);
    dl->addLayout(form);
    auto *saveRow = new QHBoxLayout;
    auto *save = new QPushButton(QStringLiteral("save settings"));
    auto *forget = new QPushButton(QStringLiteral("forget device"));
    saveRow->addWidget(save);
    saveRow->addWidget(forget);
    saveRow->addStretch();
    dl->addLayout(saveRow);

    auto *ih = new QLabel(QStringLiteral("SYNC GROUPS"));
    ih->setObjectName("h2");
    dl->addWidget(ih);
    m_itemsHint = new QLabel;
    m_itemsHint->setObjectName("dim");
    m_itemsHint->setWordWrap(true);
    dl->addWidget(m_itemsHint);
    m_items = new QListWidget;
    m_items->setMinimumHeight(140);
    dl->addWidget(m_items, 1);
    auto *removeItem = new QPushButton(QStringLiteral("remove group"));
    dl->addWidget(removeItem, 0, Qt::AlignLeft);

    split->addWidget(scroll);
    split->setSizes({280, 900});

    connect(add, &QPushButton::clicked, this, &SyncPage::addDevice);
    connect(m_devices, &QListWidget::currentRowChanged, this, &SyncPage::loadDevice);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Device location"), m_path->text());
        if (!d.isEmpty()) m_path->setText(d);
    });
    connect(save, &QPushButton::clicked, this, &SyncPage::saveDevice);
    connect(forget, &QPushButton::clicked, this, [this] {
        const DeviceProfile d = formDevice();
        if (d.id <= 0) return;
        if (QMessageBox::question(this, QStringLiteral("Forget device"),
                QStringLiteral("Forget %1 and its sync groups? Files already on it stay there.").arg(d.name)) == QMessageBox::Yes)
            m_db->deleteDevice(d.id);
    });
    connect(removeItem, &QPushButton::clicked, this, [this] {
        QListWidgetItem *it = m_items->currentItem();
        const DeviceProfile d = formDevice();
        if (!it || d.id <= 0) return;
        const SyncItem si{it->data(Qt::UserRole).toString(), it->data(Qt::UserRole + 1).toString()};
        if (QMessageBox::question(this, QStringLiteral("Remove sync group"),
                QStringLiteral("Content in \"%1\" will be removed from %2 at the next sync. Remove it?").arg(it->text(), d.name))
                == QMessageBox::Yes)
            m_db->removeSyncItem(d.id, si);
    });
    connect(m_mode, &QComboBox::currentIndexChanged, this, &SyncPage::refreshItems);
    connect(m_preview, &QPushButton::clicked, this, &SyncPage::preview);
    connect(m_sync, &QPushButton::clicked, this, &SyncPage::startSync);
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_cancelFlag = true; m_status->setText(QStringLiteral("Cancelling…")); });
    connect(m_db, &Database::devicesChanged, this, &SyncPage::refreshDevices);
    connect(&m_watcher, &QFutureWatcher<SyncReport>::finished, this, [this] {
        const SyncReport r = m_watcher.result();
        m_progress->hide();
        m_sync->setEnabled(true);
        m_preview->setEnabled(true);
        m_cancel->setEnabled(false);
        QString msg = r.cancelled ? QStringLiteral("Sync cancelled. ") : QStringLiteral("Sync complete. ");
        msg += QStringLiteral("%1 copied, %2 converted, %3 removed").arg(r.copied).arg(r.transcoded).arg(r.deleted);
        if (r.failed) msg += QStringLiteral(", %1 failed (see the log)").arg(r.failed);
        if (!r.errors.isEmpty()) msg += "\n" + r.errors.mid(0, 5).join('\n');
        m_status->setText(msg);
        emit statusMessage(msg.section('\n', 0, 0));
        // Manual mode: the queue has been delivered.
        for (const DeviceProfile &d : m_db->devices())
            if (d.id == m_syncingId && d.mode == "manual" && !r.cancelled && r.failed == 0)
                for (const SyncItem &i : m_db->syncItems(d.id)) m_db->removeSyncItem(d.id, i);
        m_syncingId = 0;
        refreshStorage();
    });
    m_poll.setInterval(4000);
    connect(&m_poll, &QTimer::timeout, this, &SyncPage::checkZunes);
    m_poll.start();
    QTimer::singleShot(1000, this, &SyncPage::checkZunes);   // after the main window has connected to us
    refreshDevices();
}

void SyncPage::checkZunes()
{
    const auto zunes = DeviceDetect::connectedZunes();
    QStringList models;
    for (const UsbDevice &z : zunes) models << z.product;
    if (models.isEmpty()) { m_zune->hide(); m_zuneModels.clear(); return; }
    const QString who = models.join(QStringLiteral(" and "));
    m_zune->setText(QStringLiteral("<b>%1 connected: sync isn't supported.</b><br>%2").arg(who.toHtmlEscaped(),
                                                                                          DeviceDetect::zuneNotice().toHtmlEscaped()));
    m_zune->show();
    if (models != m_zuneModels) {      // newly plugged in: tell the user once
        m_zuneModels = models;
        emit zuneConnected(who);
    }
}

void SyncPage::refreshDevices()
{
    const qint64 prev = m_devices->currentItem() ? m_devices->currentItem()->data(Qt::UserRole).toLongLong() : 0;
    m_devices->blockSignals(true);
    m_devices->clear();
    int select = 0;
    for (const DeviceProfile &d : m_db->devices()) {
        auto *it = new QListWidgetItem(d.name);
        it->setData(Qt::UserRole, d.id);
        it->setToolTip(d.path);
        m_devices->addItem(it);
        if (d.id == prev) select = m_devices->count() - 1;
    }
    m_devices->blockSignals(false);
    m_detail->setEnabled(m_devices->count() > 0);
    if (m_devices->count()) m_devices->setCurrentRow(select);
    loadDevice();
}

void SyncPage::loadDevice()
{
    const qint64 id = m_devices->currentItem() ? m_devices->currentItem()->data(Qt::UserRole).toLongLong() : 0;
    DeviceProfile d;
    for (const DeviceProfile &x : m_db->devices()) if (x.id == id) d = x;
    const SyncOptions o = SyncOptions::fromJson(d.options);
    m_name->setText(d.id ? d.name : QStringLiteral("no device yet"));
    m_name->setProperty("deviceId", d.id);
    m_path->setText(d.path);
    m_mode->setCurrentIndex(qMax(0, m_mode->findData(d.mode)));
    m_formats->setText(o.supportedFormats.join(", "));
    m_codec->setCurrentIndex(qMax(0, m_codec->findData(o.target.codec)));
    m_bitrate->setCurrentIndex(qMax(0, m_bitrate->findData(o.target.bitrateKbps)));
    m_threshold->setValue(o.thresholdKbps);
    m_skipDisliked->setChecked(o.skipDisliked);
    m_writePlaylists->setChecked(o.writePlaylists);
    m_status->clear();
    refreshStorage();
    refreshItems();
}

DeviceProfile SyncPage::formDevice() const
{
    DeviceProfile d;
    d.id = m_name->property("deviceId").toLongLong();
    d.name = m_name->text().trimmed();
    d.path = m_path->text().trimmed();
    d.mode = m_mode->currentData().toString();
    SyncOptions o;
    o.supportedFormats.clear();
    for (const QString &f : m_formats->text().split(QRegularExpression("[,; ]+"), Qt::SkipEmptyParts))
        o.supportedFormats << f.toLower().remove('.');
    o.target.codec = m_codec->currentData().toString();
    o.target.bitrateKbps = m_bitrate->currentData().toInt();
    o.thresholdKbps = m_threshold->value();
    o.skipDisliked = m_skipDisliked->isChecked();
    o.writePlaylists = m_writePlaylists->isChecked();
    d.options = o.toJson();
    return d;
}

void SyncPage::saveDevice()
{
    const DeviceProfile d = formDevice();
    if (d.id <= 0) return;
    m_db->saveDevice(d);
    emit statusMessage(QStringLiteral("Saved settings for %1").arg(d.name));
}

void SyncPage::addDevice()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Add device"));
    auto *l = new QVBoxLayout(&dlg);
    auto *h = new QLabel(QStringLiteral("add a device"));
    h->setObjectName("h1");
    l->addWidget(h);
    auto *list = new QListWidget;
    for (const Volume &v : DeviceDetect::volumes()) {
        auto *it = new QListWidgetItem(QStringLiteral("%1   (%2, %3 free)").arg(v.name, v.root, TrackModel::formatSize(v.free)));
        it->setData(Qt::UserRole, v.root);
        it->setData(Qt::UserRole + 1, v.name);
        list->addItem(it);
    }
    auto *none = new QLabel(list->count() ? QStringLiteral("Drives that look removable:")
                                          : QStringLiteral("No removable drives found. Choose a folder instead."));
    none->setObjectName("dim");
    l->addWidget(none);
    l->addWidget(list);
    auto *pick = new QPushButton(QStringLiteral("choose a folder…"));
    l->addWidget(pick, 0, Qt::AlignLeft);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit;
    auto *path = new QLineEdit;
    form->addRow(QStringLiteral("Name"), name);
    form->addRow(QStringLiteral("Location"), path);
    l->addLayout(form);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    l->addWidget(bb);
    connect(list, &QListWidget::currentItemChanged, &dlg, [=](QListWidgetItem *it) {
        if (!it) return;
        path->setText(it->data(Qt::UserRole).toString());
        if (name->text().isEmpty()) name->setText(it->data(Qt::UserRole + 1).toString());
    });
    connect(pick, &QPushButton::clicked, &dlg, [&] {
        const QString d = QFileDialog::getExistingDirectory(&dlg, QStringLiteral("Device location"));
        if (d.isEmpty()) return;
        path->setText(d);
        if (name->text().isEmpty()) name->setText(QFileInfo(d).fileName());
    });
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    if (!QFileInfo(path->text()).isDir()) {
        QMessageBox::warning(this, QStringLiteral("Add device"), QStringLiteral("That location isn't a folder that exists."));
        return;
    }
    DeviceProfile d;
    d.name = name->text().trimmed().isEmpty() ? QStringLiteral("My device") : name->text().trimmed();
    d.path = path->text();
    d.mode = "chosen";
    d.options = SyncOptions{}.toJson();
    const qint64 id = m_db->saveDevice(d);
    for (int i = 0; i < m_devices->count(); ++i)
        if (m_devices->item(i)->data(Qt::UserRole).toLongLong() == id) m_devices->setCurrentRow(i);
}

void SyncPage::refreshStorage()
{
    const QString path = m_path->text();
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        m_storage->setBreakdown({});
        m_storageText->setText(path.isEmpty() ? QString() : QStringLiteral("Not connected. Items you add are queued until it is."));
        return;
    }
    const StorageBreakdown b = SyncEngine::storage(path);
    m_storage->setBreakdown(b);
    m_storageText->setText(QStringLiteral("Capacity %1").arg(TrackModel::formatSize(b.total)));
}

void SyncPage::refreshItems()
{
    m_items->clear();
    const DeviceProfile d = formDevice();
    const QString mode = m_mode->currentData().toString();
    if (mode == "all")
        m_itemsHint->setText(QStringLiteral("This device gets your whole collection. Songs you remove from "
                                            "the collection are removed from the device at the next sync."));
    else if (mode == "chosen")
        m_itemsHint->setText(QStringLiteral("Right-click an artist, album, genre, playlist or song in your collection "
                                            "and choose Sync to device. Each one becomes a group here; changes to it "
                                            "are mirrored on the device."));
    else
        m_itemsHint->setText(QStringLiteral("Manual: items you send are copied right away. Nothing is removed from "
                                            "the device automatically."));
    if (d.id <= 0) return;
    for (const SyncItem &i : m_db->syncItems(d.id)) {
        QString label = i.key;
        if (i.kind == "album") label = i.key.section(QChar(0x1f), 1) + QStringLiteral(" — ") + i.key.section(QChar(0x1f), 0, 0);
        else if (i.kind == "playlist") { auto pl = m_db->playlist(i.key.toLongLong()); label = pl ? pl->name : QStringLiteral("(deleted playlist)"); }
        else if (i.kind == "track") label = QFileInfo(i.key).fileName();
        auto *it = new QListWidgetItem(QStringLiteral("%1   ·   %2").arg(i.kind, label));
        it->setData(Qt::UserRole, i.kind);
        it->setData(Qt::UserRole + 1, i.key);
        m_items->addItem(it);
    }
}

SyncPlan SyncPage::buildPlan(const DeviceProfile &d, TrackList *selected, QVector<QPair<QString, TrackList>> *playlists)
{
    const TrackList all = m_db->allTracks();
    const auto items = m_db->syncItems(d.id);
    *selected = SyncEngine::selectTracks(d, items, all, [this, &all](qint64 id) { return m_lib->playlistTracks(id, all); });
    playlists->clear();
    for (const Playlist &p : m_db->playlists()) {
        bool include = d.mode == "all";
        for (const SyncItem &i : items) if (i.kind == "playlist" && i.key.toLongLong() == p.id) include = true;
        if (include) playlists->push_back({p.name, m_lib->playlistTracks(p.id, all)});
    }
    return SyncEngine::plan(d.path, *selected, SyncOptions::fromJson(d.options), d.mode != "manual");
}

void SyncPage::preview()
{
    saveDevice();
    const DeviceProfile d = formDevice();
    if (!QFileInfo(d.path).isDir()) { m_status->setText(QStringLiteral("The device isn't connected.")); return; }
    TrackList sel;
    QVector<QPair<QString, TrackList>> pls;
    const SyncPlan p = buildPlan(d, &sel, &pls);
    QString msg = QStringLiteral("%1 songs selected. %2 to copy, %3 to convert, %4 to remove, %5 already up to date.")
        .arg(sel.size()).arg(p.count(SyncAction::Copy)).arg(p.count(SyncAction::Transcode))
        .arg(p.count(SyncAction::Delete)).arg(p.upToDate);
    if (p.skippedDisliked) msg += QStringLiteral(" %1 disliked songs skipped.").arg(p.skippedDisliked);
    msg += QStringLiteral("\nWrites about %1; %2 free.").arg(TrackModel::formatSize(p.bytesToWrite), TrackModel::formatSize(p.freeBytes));
    if (p.overfilled()) msg += QStringLiteral("\nThis won't fit. Remove some sync groups, convert to a lower bit rate, or free up space.");
    if (p.count(SyncAction::Transcode) && !Transcoder::available())
        msg += QStringLiteral("\nffmpeg wasn't found, so conversions will fail. Install it or set its path in Settings.");
    m_status->setText(msg);
}

void SyncPage::syncDevice(qint64 deviceId)
{
    for (int i = 0; i < m_devices->count(); ++i)
        if (m_devices->item(i)->data(Qt::UserRole).toLongLong() == deviceId) m_devices->setCurrentRow(i);
    startSync();
}

void SyncPage::startSync()
{
    if (m_watcher.isRunning()) return;
    saveDevice();
    const DeviceProfile d = formDevice();
    if (d.id <= 0) return;
    if (!QFileInfo(d.path).isDir()) {
        m_status->setText(QStringLiteral("%1 isn't connected. Your choices are saved and will sync when it is.").arg(d.name));
        return;
    }
    TrackList sel;
    QVector<QPair<QString, TrackList>> pls;
    const SyncPlan plan = buildPlan(d, &sel, &pls);
    if (plan.overfilled()) {
        m_status->setText(QStringLiteral("Not enough space: this sync needs about %1 but only %2 is free. "
                                         "Remove some sync groups or convert to a lower bit rate.")
                              .arg(TrackModel::formatSize(plan.bytesToWrite - plan.bytesToFree), TrackModel::formatSize(plan.freeBytes)));
        return;
    }
    if (plan.actions.isEmpty()) {
        m_status->setText(QStringLiteral("Everything is already up to date."));
        if (d.mode == "manual") for (const SyncItem &i : m_db->syncItems(d.id)) m_db->removeSyncItem(d.id, i);
        return;
    }
    m_cancelFlag = false;
    m_syncingId = d.id;
    m_progress->setRange(0, int(plan.actions.size()));
    m_progress->setValue(0);
    m_progress->show();
    m_sync->setEnabled(false);
    m_preview->setEnabled(false);
    m_cancel->setEnabled(true);
    m_status->setText(QStringLiteral("Syncing…"));
    const SyncOptions o = SyncOptions::fromJson(d.options);
    const QString root = d.path;
    auto progress = [this](int done, int total, const QString &what) {
        QMetaObject::invokeMethod(this, [this, done, total, what] {
            m_progress->setMaximum(total);
            m_progress->setValue(done);
            if (!what.isEmpty()) m_status->setText(QStringLiteral("Syncing %1 of %2: %3").arg(done + 1).arg(total).arg(what));
        }, Qt::QueuedConnection);
    };
    m_watcher.setFuture(QtConcurrent::run([root, plan, o, pls, progress, this] {
        return SyncEngine::execute(root, plan, o, pls, progress, &m_cancelFlag);
    }));
}

} // namespace halo
