#pragma once
#include "Library.h"
#include "TrackModel.h"
#include <QWidget>
#include <QHash>
#include <QIcon>
#include <QTimer>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QStackedWidget;
class QTableView;

namespace halo {

class ArtFinder;
class DiscoverView;
class MergeList;
class RadioView;
class Player;

class CollectionPage : public QWidget {
    Q_OBJECT
public:
    CollectionPage(Library *library, Player *player, QWidget *parent = nullptr);
    void focusSearch();
    void showView(const QString &name);    // artists|albums|genres|songs|playlists|now playing|radio|discover|reports
    void showPlaylist(qint64 id);
    void showArtist(const QString &name);
    void showAlbum(const QString &key);
    void showGenre(const QString &name);
    const TrackList &allTracks() const { return m_all; }
    TrackList tracksFor(const QString &kind, const QString &key) const;   // artist/album/genre/playlist

signals:
    void syncNowRequested(qint64 deviceId);
    void statusMessage(const QString &text);

private:
    struct TableBundle { QTableView *view = nullptr; TrackModel *model = nullptr; TrackFilterProxy *proxy = nullptr; };
    struct Browse { MergeList *list = nullptr; TableBundle table; QString kind; QLabel *header = nullptr; };

    QWidget *buildBrowse(Browse &b, const QString &kind);
    QWidget *buildSongs();
    QWidget *buildPlaylists();
    QWidget *buildNowPlaying();
    QWidget *buildReports();
    TableBundle makeTable(const QString &settingsKey, bool sortable = true);
    void configureColumns(QTableView *view, const QString &settingsKey, const QList<int> &defaults);

    void reload();
    void refreshBrowse(Browse &b);
    void refreshBrowseTracks(Browse &b);
    void refreshPlaylists();
    void refreshPlaylistTracks();
    void refreshNowPlaying();
    void refreshReports();
    void applySearch();

    TrackList selectedTracks(const TableBundle &t) const;
    TrackList visibleTracks(const TableBundle &t) const;
    void playFrom(const TableBundle &t, const QModelIndex &proxyIndex);
    void trackMenu(const TableBundle &t, const QPoint &pos, bool inPlaylist);
    void itemMenu(Browse &b, const QPoint &pos);
    void addDeviceMenus(QMenu *menu, const QString &kind, const QString &key, const TrackList &tracks);
    void addPlaylistMenu(QMenu *menu, const TrackList &tracks);
    void editInfo(const TrackList &tracks);
    void rate(const TrackList &tracks, int rating);
    void pin(const QString &type, const QString &key, const QString &label);
    void pasteArt(const QString &albumKey);
    void merge(const QString &kind, const QString &fromKey, const QString &fromLabel, const QString &toKey, const QString &toLabel);
    void removeFromCollection(const TrackList &tracks);
    void newPlaylist(const TrackList &initial = {});
    void newAutoPlaylist();
    void editAutoPlaylist(qint64 id);
    void importPlaylist();
    void exportPlaylist(qint64 id);
    QIcon albumIcon(const Track &t);

    Library *m_lib;
    Database *m_db;
    Player *m_player;
    TrackList m_all;
    QHash<qint64, int> m_byId;
    QHash<QString, QIcon> m_iconCache;
    QTimer m_reloadTimer;

    QLineEdit *m_search = nullptr;
    QButtonGroup *m_pivots = nullptr;
    QStackedWidget *m_stack = nullptr;
    QStringList m_viewNames;
    Browse m_artists, m_albums, m_genres;
    TableBundle m_songs;
    MergeList *m_playlistList = nullptr;
    TableBundle m_playlistTable;
    QLabel *m_playlistHeader = nullptr;
    QWidget *m_staticButtons = nullptr, *m_autoButtons = nullptr;
    TableBundle m_nowPlaying;
    QComboBox *m_reportKind = nullptr;
    TableBundle m_report;
    QLabel *m_reportInfo = nullptr;
    QLabel *m_summary = nullptr;
    RadioView *m_radio = nullptr;
    DiscoverView *m_discover = nullptr;
    ArtFinder *m_artFinder = nullptr;
};

} // namespace halo
