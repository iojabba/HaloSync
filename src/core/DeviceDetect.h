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
    // Microsoft Zune players seen on USB (Linux: sysfs, Windows: SetupAPI,
    // macOS: IOKit). Identification only; this app never talks to them (SPEC §4.1).
    // `product` holds the model name, e.g. "Zune HD".
    QVector<UsbDevice> connectedZunes();
    // Model name for a Microsoft USB product id / name, or empty if it isn't a Zune.
    QString zuneModel(const QString &productId, const QString &productName);
    QString zuneNotice();
}

} // namespace halo
