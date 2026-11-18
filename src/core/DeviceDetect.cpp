#include "DeviceDetect.h"
#include <QDir>
#include <QFile>
#include <QStorageInfo>

namespace halo {

QVector<Volume> DeviceDetect::volumes()
{
    QVector<Volume> out;
    for (const QStorageInfo &s : QStorageInfo::mountedVolumes()) {
        if (!s.isValid() || !s.isReady() || s.isReadOnly() || s.isRoot()) continue;
        const QString root = s.rootPath();
        const QByteArray fs = s.fileSystemType().toLower();
        bool removable = false;
#if defined(Q_OS_WIN)
        removable = !root.startsWith(QLatin1String("C:"), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS)
        removable = root.startsWith(QLatin1String("/Volumes/"));
#else
        removable = root.startsWith(QLatin1String("/media/")) || root.startsWith(QLatin1String("/run/media/")) ||
                    root.startsWith(QLatin1String("/mnt/"));
        if (fs == "tmpfs" || fs == "overlay" || fs == "proc" || fs == "sysfs" || fs.startsWith("fuse.portal")) continue;
#endif
        if (fs == "vfat" || fs == "exfat" || fs == "msdos" || fs == "fat32") removable = true;
        if (!removable) continue;
        Volume v;
        v.root = root;
        v.name = s.displayName().isEmpty() ? root : s.displayName();
        v.total = s.bytesTotal();
        v.free = s.bytesAvailable();
        v.likelyRemovable = removable;
        out.push_back(v);
    }
    return out;
}

QVector<UsbDevice> DeviceDetect::connectedZunes()
{
    QVector<UsbDevice> out;
#if defined(Q_OS_LINUX)
    auto readAttr = [](const QString &dir, const char *name) {
        QFile f(dir + '/' + QLatin1String(name));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    };
    QDir sys(QStringLiteral("/sys/bus/usb/devices"));
    for (const QString &e : sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = sys.filePath(e);
        const QString vendor = readAttr(dir, "idVendor");
        if (vendor.compare(QLatin1String("045e"), Qt::CaseInsensitive) != 0) continue;   // Microsoft
        const QString product = readAttr(dir, "product");
        if (!product.contains(QLatin1String("Zune"), Qt::CaseInsensitive)) continue;
        out.push_back({vendor, readAttr(dir, "idProduct"), readAttr(dir, "manufacturer"), product});
    }
#endif
    return out;
}

QString DeviceDetect::zuneNotice()
{
    return QStringLiteral(
        "A Zune player is connected. Zune players only accept file access after an authentication "
        "step that this app doesn't implement, so it can't read from or write to them. "
        "Use the official Zune software for this device. Halo Sync syncs to USB drives, SD cards "
        "and other players that show up as a drive.");
}

} // namespace halo
