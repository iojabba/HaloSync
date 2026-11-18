#include "Paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <QDateTime>
#include <QTextStream>
#include <cstdio>

namespace halo {

QString Paths::s_dataDir;
bool Paths::s_portable = false;

void Paths::init(const QString &overrideDataDir)
{
    if (!overrideDataDir.isEmpty()) {
        s_dataDir = overrideDataDir;
    } else {
        const QString appDir = QCoreApplication::applicationDirPath();
        if (QFileInfo::exists(appDir + "/portable") || QFileInfo::exists(appDir + "/portable.txt")) {
            s_portable = true;
            s_dataDir = appDir + "/data";
        } else {
            s_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (s_dataDir.isEmpty()) s_dataDir = QDir::homePath() + "/.halo-sync";
        }
    }
    QDir().mkpath(s_dataDir);
    QDir().mkpath(transcodeCacheDir());
}

bool Paths::portable() { return s_portable; }
QString Paths::dataDir() { if (s_dataDir.isEmpty()) init(); return s_dataDir; }
QString Paths::databaseFile() { return dataDir() + "/library.sqlite"; }
QString Paths::logFile() { return dataDir() + "/halo-sync.log"; }

QString Paths::transcodeCacheDir()
{
    auto s = settings();
    const QString custom = s->value("sync/cacheDir").toString();
    return custom.isEmpty() ? dataDir() + "/transcode-cache" : custom;
}

std::unique_ptr<QSettings> Paths::settings()
{
    return std::make_unique<QSettings>(dataDir() + "/settings.ini", QSettings::IniFormat);
}

void logLine(const QString &category, const QString &message)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    const QString line = QStringLiteral("%1 [%2] %3")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate), category, message);
    std::fprintf(stderr, "%s\n", qPrintable(line));
    QFile f(Paths::logFile());
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&f) << line << '\n';
    }
}

} // namespace halo
