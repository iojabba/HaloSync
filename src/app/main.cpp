#include "CollectionPage.h"
#include "Database.h"
#include "Library.h"
#include "MainWindow.h"
#include "Paths.h"
#include "Player.h"
#include "SyncEngine.h"
#include "Theme.h"
#include "TrackModel.h"
#ifdef HALO_HAVE_MPRIS
#include "Mpris.h"
#endif
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>

using namespace halo;

namespace {
void out(const QString &s) { std::fprintf(stdout, "%s\n", qPrintable(s)); std::fflush(stdout); }

int runSync(Library &lib, const QString &target, const QString &mode)
{
    Database *db = lib.db();
    DeviceProfile dev;
    for (const DeviceProfile &d : db->devices()) if (d.path == target) dev = d;
    if (!dev.id) {
        dev.name = QFileInfo(target).fileName().isEmpty() ? QStringLiteral("Device") : QFileInfo(target).fileName();
        dev.path = target;
        dev.mode = mode.isEmpty() ? QStringLiteral("all") : mode;
        dev.options = SyncOptions{}.toJson();
        dev.id = db->saveDevice(dev);
    } else if (!mode.isEmpty()) {
        dev.mode = mode;
        db->saveDevice(dev);
    }
    const TrackList all = db->allTracks();
    const TrackList sel = SyncEngine::selectTracks(dev, db->syncItems(dev.id), all,
                                                   [&](qint64 id) { return lib.playlistTracks(id, all); });
    const SyncOptions o = SyncOptions::fromJson(dev.options);
    const SyncPlan plan = SyncEngine::plan(dev.path, sel, o, dev.mode != "manual");
    out(QStringLiteral("plan: %1 copy, %2 convert, %3 remove, %4 up to date, writes %5, free %6")
            .arg(plan.count(SyncAction::Copy)).arg(plan.count(SyncAction::Transcode)).arg(plan.count(SyncAction::Delete))
            .arg(plan.upToDate).arg(TrackModel::formatSize(plan.bytesToWrite), TrackModel::formatSize(plan.freeBytes)));
    if (plan.overfilled()) { out("error: not enough space on the device"); return 3; }
    QVector<QPair<QString, TrackList>> pls;
    for (const Playlist &p : db->playlists()) pls.push_back({p.name, lib.playlistTracks(p.id, all)});
    const SyncReport r = SyncEngine::execute(dev.path, plan, o, pls);
    out(QStringLiteral("sync: %1 copied, %2 converted, %3 removed, %4 failed").arg(r.copied).arg(r.transcoded).arg(r.deleted).arg(r.failed));
    for (const QString &e : r.errors) out("  " + e);
    return r.failed ? 2 : 0;
}
} // namespace

int main(int argc, char *argv[])
{
    // Command-line jobs (--scan, --list, --sync-to) must work without a display.
    for (int i = 1; i < argc; ++i) {
        const QByteArray a(argv[i]);
        if ((a == "--scan" || a == "--list" || a == "--sync-to") && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Halo Sync"));
    QApplication::setOrganizationName(QStringLiteral("HaloSync"));
    QApplication::setApplicationVersion(QStringLiteral(HALO_VERSION_STRING));
    QApplication::setDesktopFileName(QStringLiteral("halo-sync"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Halo Sync: music library, player and USB sync."));
    cli.addHelpOption();
    cli.addVersionOption();
    QCommandLineOption dataDir("data-dir", "Keep the library and settings in <dir>.", "dir");
    QCommandLineOption addFolder("add-folder", "Add a monitored music folder.", "dir");
    QCommandLineOption scan("scan", "Scan monitored folders and exit.");
    QCommandLineOption list("list", "Print the collection and exit.");
    QCommandLineOption syncTo("sync-to", "Sync to the device or folder at <dir> and exit.", "dir");
    QCommandLineOption syncMode("sync-mode", "Sync mode for --sync-to: all, chosen or manual.", "mode");
    QCommandLineOption shot("screenshot", "Open the window, save a screenshot to <file> and exit.", "file");
    QCommandLineOption page("page", "Page to open: quickplay, collection, sync or settings.", "name");
    QCommandLineOption view("view", "Collection view: artists, albums, genres, songs, playlists, now playing, reports.", "name");
    cli.addOptions({dataDir, addFolder, scan, list, syncTo, syncMode, shot, page, view});
    cli.process(app);

    Paths::init(cli.value(dataDir));
    Database db;
    if (!db.open(Paths::databaseFile())) {
        const QString msg = QStringLiteral("Couldn't open the library database at %1: %2").arg(Paths::databaseFile(), db.lastError());
        if (cli.isSet(scan) || cli.isSet(list) || cli.isSet(syncTo)) { out(msg); return 1; }
        QMessageBox::critical(nullptr, QStringLiteral("Halo Sync"), msg);
        return 1;
    }
    Library library(&db);
    logLine("app", QStringLiteral("start %1, data in %2").arg(QApplication::applicationVersion(), Paths::dataDir()));

    for (const QString &f : cli.values(addFolder)) {
        const QString err = library.addFolder(f);
        out(err.isEmpty() ? QStringLiteral("monitoring %1").arg(f) : QStringLiteral("can't add %1: %2").arg(f, err));
    }

    const bool headless = cli.isSet(scan) || cli.isSet(list) || cli.isSet(syncTo) || (cli.isSet(addFolder) && !cli.isSet(shot));
    if (headless) {
        if (cli.isSet(scan) || cli.isSet(addFolder)) {
            const ScanResult r = library.rescanBlocking();
            out(QStringLiteral("scan: %1 new or changed, %2 removed, %3 unchanged").arg(r.changed.size()).arg(r.removed.size()).arg(r.unchanged));
        }
        if (cli.isSet(list)) {
            for (const Track &t : db.allTracks())
                out(QStringLiteral("%1\t%2\t%3\t%4\t%5\t%6").arg(t.displayAlbumArtist(), t.displayAlbum())
                        .arg(t.trackNo).arg(t.displayTitle(), TrackModel::formatDuration(t.durationMs), t.format));
        }
        if (cli.isSet(syncTo)) return runSync(library, cli.value(syncTo), cli.value(syncMode));
        return 0;
    }

    Theme::apply(app);
    Player player(&db);
    MainWindow window(&library, &player);
#ifdef HALO_HAVE_MPRIS
    QObject *mprisRoot = nullptr;
    if (registerMpris(&app, &player, &mprisRoot) && mprisRoot)
        QObject::connect(static_cast<MprisRoot *>(mprisRoot), &MprisRoot::raiseRequested, &window, [&window] {
            window.show(); window.raise(); window.activateWindow(); });
#endif
    if (cli.isSet(page)) window.showPage(cli.value(page));
    if (cli.isSet(view)) window.collection()->showView(cli.value(view));
    window.show();

    // First run: offer the standard Music folder (SPEC §2.15 "start with defaults").
    if (db.folders().isEmpty() && !cli.isSet(shot)) {
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!music.isEmpty() && QFileInfo(music).isDir() &&
            QMessageBox::question(&window, QStringLiteral("Welcome to Halo Sync"),
                QStringLiteral("Add your Music folder (%1) to the collection? You can add or change folders "
                               "later in Settings.").arg(QDir::toNativeSeparators(music))) == QMessageBox::Yes)
            library.addFolder(music);
        else
            window.showPage("settings");
    } else {
        library.rescan();   // pick up changes made while closed
    }

    if (cli.isSet(shot)) {
        const QString file = cli.value(shot);
        QTimer::singleShot(2500, &window, [&window, file] {
            window.grab().save(file);
            out(QStringLiteral("saved %1").arg(file));
            qApp->quit();
        });
    }
    return app.exec();
}
