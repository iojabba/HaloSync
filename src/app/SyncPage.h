#pragma once
#include "SyncEngine.h"
#include <QFutureWatcher>
#include <QTimer>
#include <QWidget>
#include <atomic>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;

class QPaintEvent;

namespace halo {

class Library;

class StorageBar : public QWidget {
    Q_OBJECT
public:
    explicit StorageBar(QWidget *parent = nullptr);
    void setBreakdown(const StorageBreakdown &b);
    QSize sizeHint() const override { return {400, 46}; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    StorageBreakdown m_b;
};

// Devices and sync (SPEC §2.12, §4.1).
class SyncPage : public QWidget {
    Q_OBJECT
public:
    SyncPage(Library *library, QWidget *parent = nullptr);
    bool isSyncing() const { return m_watcher.isRunning(); }
public slots:
    void syncDevice(qint64 deviceId);
signals:
    void statusMessage(const QString &text);

private:
    void refreshDevices();
    void loadDevice();
    DeviceProfile formDevice() const;
    void saveDevice();
    void addDevice();
    void refreshStorage();
    void refreshItems();
    void preview();
    void startSync();
    void checkZunes();
    SyncPlan buildPlan(const DeviceProfile &d, TrackList *selected, QVector<QPair<QString, TrackList>> *playlists);

    Library *m_lib;
    Database *m_db;
    QListWidget *m_devices;
    QLabel *m_zune;
    QWidget *m_detail;
    QLineEdit *m_name, *m_path, *m_formats;
    QComboBox *m_mode, *m_codec, *m_bitrate;
    QSpinBox *m_threshold;
    QCheckBox *m_skipDisliked, *m_writePlaylists;
    StorageBar *m_storage;
    QLabel *m_storageText;
    QListWidget *m_items;
    QLabel *m_itemsHint;
    QPushButton *m_sync, *m_cancel, *m_preview;
    QProgressBar *m_progress;
    QLabel *m_status;
    QFutureWatcher<SyncReport> m_watcher;
    std::atomic_bool m_cancelFlag{false};
    qint64 m_syncingId = 0;
    QTimer m_poll;
};

} // namespace halo
