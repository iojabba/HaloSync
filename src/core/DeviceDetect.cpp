#include "DeviceDetect.h"
#include <QDir>
#include <QFile>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QSet>

#if defined(Q_OS_WIN)
#  include <windows.h>
#  include <setupapi.h>
#elif defined(Q_OS_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <IOKit/IOKitLib.h>
#  include <IOKit/usb/IOUSBLib.h>
#endif

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

QString DeviceDetect::zuneModel(const QString &productId, const QString &productName)
{
    // USB product ids of Zune players (vendor 045e, Microsoft).
    const QString pid = productId.toLower();
    if (pid == QLatin1String("063e")) return QStringLiteral("Zune HD");
    if (pid == QLatin1String("0710")) return QStringLiteral("Zune");
    if (productName.contains(QLatin1String("Zune"), Qt::CaseInsensitive)) return productName.trimmed();
    return {};
}

QVector<UsbDevice> DeviceDetect::connectedZunes()
{
    QVector<UsbDevice> out;
    QSet<QString> seen;
    auto add = [&](const QString &vid, const QString &pid, const QString &maker, const QString &name) {
        if (vid.compare(QLatin1String("045e"), Qt::CaseInsensitive) != 0) return;   // Microsoft only
        const QString model = zuneModel(pid, name);
        if (model.isEmpty()) return;
        if (seen.contains(pid.toLower())) return;   // one entry per player (USB and MTP nodes both match)
        seen.insert(pid.toLower());
        out.push_back({vid.toLower(), pid.toLower(), maker, model});
    };
#if defined(Q_OS_LINUX)
    auto readAttr = [](const QString &dir, const char *name) {
        QFile f(dir + '/' + QLatin1String(name));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    };
    QDir sys(QStringLiteral("/sys/bus/usb/devices"));
    for (const QString &e : sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString dir = sys.filePath(e);
        add(readAttr(dir, "idVendor"), readAttr(dir, "idProduct"), readAttr(dir, "manufacturer"), readAttr(dir, "product"));
    }
#elif defined(Q_OS_WIN)
    // Every present device; match on hardware ids like USB\VID_045E&PID_063E.
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set != INVALID_HANDLE_VALUE) {
        static const QRegularExpression ids(QStringLiteral("VID_([0-9A-F]{4})&PID_([0-9A-F]{4})"),
                                            QRegularExpression::CaseInsensitiveOption);
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof info;
        auto prop = [&](DWORD which) {
            wchar_t buf[2048] = {};
            DWORD type = 0;
            if (!SetupDiGetDeviceRegistryPropertyW(set, &info, which, &type, reinterpret_cast<PBYTE>(buf), sizeof buf - 2, nullptr))
                return QString();
            // REG_MULTI_SZ: join the strings.
            QString s;
            for (const wchar_t *p = buf; *p; p += wcslen(p) + 1) s += QString::fromWCharArray(p) + ' ';
            return s.trimmed();
        };
        for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
            const QString hw = prop(SPDRP_HARDWAREID);
            const auto m = ids.match(hw);
            if (!m.hasMatch()) continue;
            QString name = prop(SPDRP_FRIENDLYNAME);
            if (name.isEmpty()) name = prop(SPDRP_DEVICEDESC);
            add(m.captured(1), m.captured(2), prop(SPDRP_MFG), name);
        }
        SetupDiDestroyDeviceInfoList(set);
    }
#elif defined(Q_OS_MACOS)
    io_iterator_t it = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOUSBHostDevice"), &it) == KERN_SUCCESS) {
        auto strProp = [](io_object_t dev, const char *key) {
            QString out;
            CFStringRef k = CFStringCreateWithCString(nullptr, key, kCFStringEncodingUTF8);
            if (CFTypeRef v = IORegistryEntryCreateCFProperty(dev, k, nullptr, 0)) {
                if (CFGetTypeID(v) == CFStringGetTypeID()) {
                    char buf[512] = {};
                    CFStringGetCString(static_cast<CFStringRef>(v), buf, sizeof buf, kCFStringEncodingUTF8);
                    out = QString::fromUtf8(buf);
                } else if (CFGetTypeID(v) == CFNumberGetTypeID()) {
                    int n = 0;
                    CFNumberGetValue(static_cast<CFNumberRef>(v), kCFNumberIntType, &n);
                    out = QStringLiteral("%1").arg(n, 4, 16, QChar('0'));
                }
                CFRelease(v);
            }
            CFRelease(k);
            return out;
        };
        while (io_object_t dev = IOIteratorNext(it)) {
            add(strProp(dev, "idVendor"), strProp(dev, "idProduct"), strProp(dev, "USB Vendor Name"), strProp(dev, "USB Product Name"));
            IOObjectRelease(dev);
        }
        IOObjectRelease(it);
    }
#endif
    return out;
}

QString DeviceDetect::zuneNotice()
{
    return QStringLiteral(
        "Halo Sync can't sync with Zune players. A Zune only lets a computer read or write its music after "
        "a Microsoft authentication step (MTPZ) that Halo Sync deliberately doesn't implement. "
        "Use the Zune software for this device. Halo Sync syncs to USB drives, SD cards and other "
        "players that show up as a drive.");
}

} // namespace halo
