#pragma once
#include <QString>
#include <QVector>

namespace halo {

struct Volume {
    QString root;
    QString name;
    qint64  total = 0;
    qint64  free = 0;
    bool    likelyRemovable = false;
};

struct UsbDevice {
    QString vendorId, productId, manufacturer, product;
};

namespace DeviceDetect {
    // Mounted volumes that could be a USB player or SD card.
    QVector<Volume> volumes();
    // Microsoft Zune players seen on USB (Linux: sysfs). Identification only;
    // this app does not talk to them (see SPEC §4.1).
    QVector<UsbDevice> connectedZunes();
    QString zuneNotice();
}

} // namespace halo
