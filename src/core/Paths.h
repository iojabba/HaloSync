#pragma once
#include <QString>
#include <QSettings>
#include <memory>

namespace halo {

// Resolves where data lives. Portable mode: if a file named "portable" (or
// "portable.txt") sits next to the executable, all data is kept in ./data.
class Paths {
public:
    static void init(const QString &overrideDataDir = {});
    static bool portable();
    static QString dataDir();
    static QString databaseFile();
    static QString transcodeCacheDir();
    static QString logFile();
    static std::unique_ptr<QSettings> settings();
private:
    static QString s_dataDir;
    static bool s_portable;
};

// Appends a line to the log file (and stderr). Thread-safe.
void logLine(const QString &category, const QString &message);

} // namespace halo
