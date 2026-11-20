#include "CollectionPage.h"
#include "ArtFinder.h"
#include "DiscoverView.h"
#include "DuplicateDialog.h"
#include "LyricsPanel.h"
#include "RadioView.h"
#include "AlbumLookupDialog.h"
#include "AutoPlaylistDialog.h"
#include "MergeList.h"
#include "Paths.h"
#include "PlaylistIO.h"
#include "Player.h"
#include "Reports.h"
#include "TagEditorDialog.h"
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

namespace halo {

namespace {
const QChar kSep(0x1f);
QString albumKey(const Track &t) { return t.displayAlbumArtist() + kSep + t.displayAlbum(); }

void sortAlbumOrder(TrackList &list)
{
    std::stable_sort(list.begin(), list.end(), [](const Track &a, const Track &b) {
        if (int c = a.displayAlbumArtist().compare(b.displayAlbumArtist(), Qt::CaseInsensitive)) return c < 0;
        if (a.year != b.year && !a.album.isEmpty()) return a.year < b.year;
        if (int c = a.displayAlbum().compare(b.displayAlbum(), Qt::CaseInsensitive)) return c < 0;
        if (a.discNo != b.discNo) return a.discNo < b.discNo;
        return a.trackNo < b.trackNo;
    });
}

QPushButton *flatButton(const QString &text)
{
    auto *b = new QPushButton(text);
    b->setObjectName("flat");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
} // namespace

CollectionPage::CollectionPage(Library *library, Player *player, QWidget *parent)
    : QWidget(parent), m_lib(library), m_db(library->db()), m_player(player)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    // Sub-pivots + search
    auto *top = new QHBoxLayout;
    m_pivots = new QButtonGroup(this);
    m_viewNames = {"artists", "albums", "genres", "songs", "playlists", "now playing", "radio", "discover", "reports"};
    for (int i = 0; i < m_viewNames.size(); ++i) {
        auto *b = new QPushButton(m_viewNames[i]);
        b->setObjectName("subpivot");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        m_pivots->addButton(b, i);
        top->addWidget(b);
    }
    top->addStretch();
    m_summary = new QLabel;
    m_summary->setObjectName("dim");
    top->addWidget(m_summary);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("search collection"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(240);
    top->addWidget(m_search);
    lay->addLayout(top);

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildBrowse(m_artists, "artist"));
    m_stack->addWidget(buildBrowse(m_albums, "album"));
    m_stack->addWidget(buildBrowse(m_genres, "genre"));
    m_stack->addWidget(buildSongs());
    m_stack->addWidget(buildPlaylists());
    m_stack->addWidget(buildNowPlaying());
    m_radio = new RadioView(m_db, m_player);
    connect(m_radio, &RadioView::statusMessage, this, &CollectionPage::statusMessage);
    m_stack->addWidget(m_radio);
    m_discover = new DiscoverView(m_db, m_player, [this](const Track &t) { return albumIcon(t); });
    m_stack->addWidget(m_discover);
    m_stack->addWidget(buildReports());
    lay->addWidget(m_stack, 1);

    connect(m_pivots, &QButtonGroup::idClicked, this, [this](int id) {
        m_stack->setCurrentIndex(id);
        Paths::settings()->setValue("ui/collectionViewName", m_viewNames[id]);
        if (m_viewNames[id] == QLatin1String("reports")) refreshReports();
    });
    const int startView = qMax(0, int(m_viewNames.indexOf(Paths::settings()->value("ui/collectionViewName", "artists").toString())));
    m_pivots->button(startView)->setChecked(true);
    m_stack->setCurrentIndex(startView);

    // Missing covers are looked up after each scan (Settings › automatic album art).
    m_artFinder = new ArtFinder(m_lib, this);
    connect(m_lib, &Library::scanFinished, this, [this] { if (ArtFinder::enabled()) m_artFinder->start(); });
    connect(m_artFinder, &ArtFinder::progress, this, [this](int done, int total) {
        emit statusMessage(QStringLiteral("Looking up album art: %1 of %2").arg(done).arg(total));
    });
    connect(m_artFinder, &ArtFinder::finished, this, [this](int found, int tried) {
        if (tried) emit statusMessage(QStringLiteral("Found album art for %1 of %2 albums.").arg(found).arg(tried));
        if (found) { m_iconCache.clear(); m_reloadTimer.start(); }
    });

    connect(m_search, &QLineEdit::textChanged, this, &CollectionPage::applySearch);
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(150);
    connect(&m_reloadTimer, &QTimer::timeout, this, &CollectionPage::reload);
    connect(m_db, &Database::tracksChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(m_db, &Database::playlistsChanged, this, &CollectionPage::refreshPlaylists);
    connect(m_player, &Player::queueChanged, this, &CollectionPage::refreshNowPlaying);
    connect(m_player, &Player::currentChanged, this, [this] {
        const qint64 id = m_player->current() ? m_player->current()->id : 0;
        for (TrackModel *m : {m_artists.table.model, m_albums.table.model, m_genres.table.model, m_songs.model,
                              m_playlistTable.model, m_nowPlaying.model, m_report.model})
            m->setNowPlayingId(id);
    });
    reload();
}

// ------------------------------------------------------------------ building
CollectionPage::TableBundle CollectionPage::makeTable(const QString &settingsKey, bool sortable)
{
    TableBundle t;
    t.model = new TrackModel(this);
    t.proxy = new TrackFilterProxy(this);
    t.proxy->setSourceModel(t.model);
    t.view = new QTableView;
    t.view->setModel(t.proxy);
    t.view->setSelectionBehavior(QAbstractItemView::SelectRows);
    t.view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t.view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t.view->setShowGrid(false);
    t.view->setWordWrap(false);
    t.view->setAlternatingRowColors(false);
    t.view->verticalHeader()->hide();
    t.view->verticalHeader()->setDefaultSectionSize(26);
    t.view->horizontalHeader()->setHighlightSections(false);
    t.view->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    t.view->horizontalHeader()->setSectionsMovable(true);
    t.view->horizontalHeader()->setStretchLastSection(false);
    t.view->setSortingEnabled(sortable);
    t.view->setDragEnabled(true);
    t.view->setDragDropMode(QAbstractItemView::DragOnly);
    t.view->setContextMenuPolicy(Qt::CustomContextMenu);
    t.view->setObjectName(settingsKey);

    const TableBundle copy = t;
    connect(t.view, &QTableView::doubleClicked, this, [this, copy](const QModelIndex &i) { playFrom(copy, i); });
    return t;
}

void CollectionPage::configureColumns(QTableView *view, const QString &settingsKey, const QList<int> &defaults)
{
    auto s = Paths::settings();
    QList<int> visible;
    const QVariantList saved = s->value("columns/" + settingsKey).toList();
    for (const QVariant &v : saved) visible << v.toInt();
    if (visible.isEmpty()) visible = defaults;
    for (int c = 0; c < TrackModel::ColumnCount; ++c) view->setColumnHidden(c, !visible.contains(c));
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    view->horizontalHeader()->setSectionResizeMode(TrackModel::Title, QHeaderView::Stretch);
    view->setColumnWidth(TrackModel::Rating, 76);
    view->setColumnWidth(TrackModel::TrackNo, 44);
    view->setColumnWidth(TrackModel::Length, 70);
    for (int c : {int(TrackModel::Artist), int(TrackModel::Album), int(TrackModel::AlbumArtist)}) view->setColumnWidth(c, 190);

    // SPEC §2.3: right-click the column headings to choose fields.
    view->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(view->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, [view, settingsKey](const QPoint &pos) {
        QMenu menu;
        for (int c = 0; c < TrackModel::ColumnCount; ++c) {
            QAction *a = menu.addAction(TrackModel::columnName(c));
            a->setCheckable(true);
            a->setChecked(!view->isColumnHidden(c));
            a->setEnabled(c != TrackModel::Title);
            QObject::connect(a, &QAction::toggled, view, [view, c, settingsKey](bool on) {
                view->setColumnHidden(c, !on);
                QVariantList cols;
                for (int i = 0; i < TrackModel::ColumnCount; ++i) if (!view->isColumnHidden(i)) cols << i;
                Paths::settings()->setValue("columns/" + settingsKey, cols);
            });
        }
        menu.exec(view->horizontalHeader()->mapToGlobal(pos));
    });
}

QWidget *CollectionPage::buildBrowse(Browse &b, const QString &kind)
{
    b.kind = kind;
    auto *split = new QSplitter;
    b.list = new MergeList;
    b.list->setContextMenuPolicy(Qt::CustomContextMenu);
    if (kind == "album") b.list->setIconSize(QSize(44, 44));
    split->addWidget(b.list);

    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(12, 0, 0, 0);
    b.header = new QLabel;
    b.header->setObjectName("h1");
    rl->addWidget(b.header);
    b.table = makeTable(kind + "Tracks", true);
    QList<int> cols = {TrackModel::TrackNo, TrackModel::Title, TrackModel::Length, TrackModel::Rating, TrackModel::Artist};
    if (kind != "album") cols << TrackModel::Album;
    configureColumns(b.table.view, kind + "Tracks", cols);
    rl->addWidget(b.table.view, 1);
    split->addWidget(right);
    split->setStretchFactor(1, 3);
    split->setSizes({300, 900});

    Browse *bp = &b;
    connect(b.list, &QListWidget::currentItemChanged, this, [this, bp] { refreshBrowseTracks(*bp); });
    connect(b.list, &QListWidget::itemDoubleClicked, this, [this, bp](QListWidgetItem *it) {
        m_player->playTracks(tracksFor(bp->kind, it->data(MergeList::KeyRole).toString()));
    });
    connect(b.list, &QWidget::customContextMenuRequested, this, [this, bp](const QPoint &p) { itemMenu(*bp, p); });
    connect(b.list, &MergeList::mergeRequested, this, [this, bp](const QString &fk, const QString &fl, const QString &tk, const QString &tl) {
        merge(bp->kind, fk, fl, tk, tl);
    });
    const TableBundle tb = b.table;
    connect(b.table.view, &QWidget::customContextMenuRequested, this, [this, tb](const QPoint &p) { trackMenu(tb, p, false); });
    return split;
}

QWidget *CollectionPage::buildSongs()
{
    m_songs = makeTable("songs", true);
    configureColumns(m_songs.view, "songs", {TrackModel::Title, TrackModel::Artist, TrackModel::Album, TrackModel::Genre,
                                             TrackModel::Length, TrackModel::Rating, TrackModel::PlayCount, TrackModel::Year});
    m_songs.view->sortByColumn(TrackModel::AlbumArtist, Qt::AscendingOrder);
    const TableBundle tb = m_songs;
    connect(m_songs.view, &QWidget::customContextMenuRequested, this, [this, tb](const QPoint &p) { trackMenu(tb, p, false); });
    return m_songs.view;
}

QWidget *CollectionPage::buildPlaylists()
{
    auto *split = new QSplitter;
    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    auto *row1 = new QHBoxLayout;
    auto *newPl = flatButton(QStringLiteral("+ playlist"));
    auto *newAuto = flatButton(QStringLiteral("+ autoplaylist"));
    auto *imp = flatButton(QStringLiteral("import"));
    row1->addWidget(newPl);
    row1->addWidget(newAuto);
    row1->addWidget(imp);
    row1->addStretch();
    ll->addLayout(row1);
    m_playlistList = new MergeList;
    m_playlistList->setMergeEnabled(false);
    m_playlistList->setTrackDropEnabled(true);
    m_playlistList->setContextMenuPolicy(Qt::CustomContextMenu);
    ll->addWidget(m_playlistList, 1);
    auto *hint = new QLabel(QStringLiteral("Drag songs from any view onto a playlist to add them."));
    hint->setObjectName("dim");
    hint->setWordWrap(true);
    ll->addWidget(hint);
    split->addWidget(left);

    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(12, 0, 0, 0);
    m_playlistHeader = new QLabel;
    m_playlistHeader->setObjectName("h1");
    rl->addWidget(m_playlistHeader);
    auto *actions = new QHBoxLayout;
    auto *play = flatButton(QStringLiteral("play"));
    auto *rename = flatButton(QStringLiteral("rename"));
    auto *exp = flatButton(QStringLiteral("export"));
    auto *del = flatButton(QStringLiteral("delete"));
    m_staticButtons = new QWidget;
    auto *sl = new QHBoxLayout(m_staticButtons);
    sl->setContentsMargins(0, 0, 0, 0);
    auto *up = flatButton(QStringLiteral("move up"));
    auto *down = flatButton(QStringLiteral("move down"));
    auto *dedupe = flatButton(QStringLiteral("remove duplicates"));
    sl->addWidget(up);
    sl->addWidget(down);
    sl->addWidget(dedupe);
    m_autoButtons = new QWidget;
    auto *al = new QHBoxLayout(m_autoButtons);
    al->setContentsMargins(0, 0, 0, 0);
    auto *editRules = flatButton(QStringLiteral("edit filters"));
    auto *saveStatic = flatButton(QStringLiteral("save as playlist"));
    al->addWidget(editRules);
    al->addWidget(saveStatic);
    for (QWidget *w : {static_cast<QWidget *>(play), static_cast<QWidget *>(rename), static_cast<QWidget *>(exp),
                       static_cast<QWidget *>(del), m_staticButtons, m_autoButtons})
        actions->addWidget(w);
    actions->addStretch();
    rl->addLayout(actions);
    m_playlistTable = makeTable("playlistTracks", false);
    configureColumns(m_playlistTable.view, "playlistTracks", {TrackModel::Title, TrackModel::Artist, TrackModel::Album,
                                                              TrackModel::Length, TrackModel::Rating});
    rl->addWidget(m_playlistTable.view, 1);
    split->addWidget(right);
    split->setSizes({300, 900});

    auto currentId = [this] { return m_playlistList->currentKey().toLongLong(); };
    connect(newPl, &QPushButton::clicked, this, [this] { newPlaylist(); });
    connect(newAuto, &QPushButton::clicked, this, &CollectionPage::newAutoPlaylist);
    connect(imp, &QPushButton::clicked, this, &CollectionPage::importPlaylist);
    connect(m_playlistList, &QListWidget::currentItemChanged, this, &CollectionPage::refreshPlaylistTracks);
    connect(m_playlistList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        m_player->playTracks(m_lib->playlistTracks(it->data(MergeList::KeyRole).toLongLong(), m_all));
    });
    connect(m_playlistList, &MergeList::tracksDropped, this, [this](const QString &key, const QVector<qint64> &ids) {
        auto pl = m_db->playlist(key.toLongLong());
        if (!pl) return;
        if (pl->kind == "auto") {
            emit statusMessage(QStringLiteral("Autoplaylists change only through their filters."));
            return;
        }
        m_db->appendToPlaylist(pl->id, ids);
        emit statusMessage(ids.size() == 1 ? QStringLiteral("Added 1 item to %1").arg(pl->name)
                                           : QStringLiteral("Added %1 items to %2").arg(ids.size()).arg(pl->name));
    });
    connect(play, &QPushButton::clicked, this, [this, currentId] { m_player->playTracks(m_lib->playlistTracks(currentId(), m_all)); });
    connect(rename, &QPushButton::clicked, this, [this, currentId] {
        auto pl = m_db->playlist(currentId());
        if (!pl) return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename playlist"), QStringLiteral("Name"),
                                                   QLineEdit::Normal, pl->name, &ok);
        if (ok && !m_db->renamePlaylist(pl->id, name))
            QMessageBox::warning(this, QStringLiteral("Rename playlist"),
                                 QStringLiteral("You already have a playlist called \"%1\", or the name isn't valid.").arg(name));
    });
    connect(exp, &QPushButton::clicked, this, [this, currentId] { exportPlaylist(currentId()); });
    connect(del, &QPushButton::clicked, this, [this, currentId] {
        auto pl = m_db->playlist(currentId());
        if (pl && QMessageBox::question(this, QStringLiteral("Delete playlist"),
                                        QStringLiteral("Delete the playlist \"%1\"? The songs stay in your collection.").arg(pl->name))
                      == QMessageBox::Yes)
            m_db->deletePlaylist(pl->id);
    });
    auto move = [this, currentId](int delta) {
        const qint64 id = currentId();
        QVector<qint64> items = m_db->playlistItems(id);
        const QModelIndexList rows = m_playlistTable.view->selectionModel()->selectedRows();
        if (rows.size() != 1) return;
        const int r = rows.first().row();
        const int to = r + delta;
        if (to < 0 || to >= items.size()) return;
        items.swapItemsAt(r, to);
        m_db->setPlaylistItems(id, items);
        m_playlistTable.view->selectRow(to);
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
    connect(dedupe, &QPushButton::clicked, this, [this, currentId] {
        QVector<qint64> items = m_db->playlistItems(currentId()), out;
        QSet<qint64> seen;
        for (qint64 i : items) if (!seen.contains(i)) { seen.insert(i); out << i; }
        m_db->setPlaylistItems(currentId(), out);
        emit statusMessage(QStringLiteral("Removed %1 duplicate entries").arg(items.size() - out.size()));
    });
    connect(editRules, &QPushButton::clicked, this, [this, currentId] { editAutoPlaylist(currentId()); });
    connect(saveStatic, &QPushButton::clicked, this, [this, currentId] {
        newPlaylist(m_lib->playlistTracks(currentId(), m_all));
    });
    connect(m_playlistList, &QWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        QListWidgetItem *it = m_playlistList->itemAt(p);
        if (!it) return;
        const QString key = it->data(MergeList::KeyRole).toString();
        QMenu menu;
        menu.addAction(QStringLiteral("Play"), this, [this, key] { m_player->playTracks(m_lib->playlistTracks(key.toLongLong(), m_all)); });
        menu.addAction(QStringLiteral("Add to now playing"), this, [this, key] { m_player->enqueue(m_lib->playlistTracks(key.toLongLong(), m_all)); });
        menu.addAction(QStringLiteral("Pin to Quickplay"), this, [this, key, it] { pin("playlist", key, it->text()); });
        addDeviceMenus(&menu, "playlist", key, m_lib->playlistTracks(key.toLongLong(), m_all));
        menu.exec(m_playlistList->mapToGlobal(p));
    });
    const TableBundle tb = m_playlistTable;
    connect(m_playlistTable.view, &QWidget::customContextMenuRequested, this, [this, tb](const QPoint &p) { trackMenu(tb, p, true); });
    return split;
}

QWidget *CollectionPage::buildNowPlaying()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *row = new QHBoxLayout;
    auto *h = new QLabel(QStringLiteral("now playing"));
    h->setObjectName("h1");
    row->addWidget(h);
    row->addStretch();
    auto *save = flatButton(QStringLiteral("save as playlist"));
    auto *clear = flatButton(QStringLiteral("clear"));
    row->addWidget(save);
    row->addWidget(clear);
    l->addLayout(row);
    auto *lyricsBtn = flatButton(QStringLiteral("lyrics"));
    lyricsBtn->setCheckable(true);
    row->insertWidget(row->count() - 2, lyricsBtn);
    m_nowPlaying = makeTable("nowPlaying", false);
    configureColumns(m_nowPlaying.view, "nowPlaying", {TrackModel::Title, TrackModel::Artist, TrackModel::Album, TrackModel::Length});
    auto *npSplit = new QSplitter;
    npSplit->setHandleWidth(1);
    npSplit->addWidget(m_nowPlaying.view);
    auto *lyrics = new LyricsPanel(m_player);
    npSplit->addWidget(lyrics);
    npSplit->setStretchFactor(0, 3);
    npSplit->setStretchFactor(1, 2);
    l->addWidget(npSplit, 1);
    const bool showLyrics = Paths::settings()->value("ui/showLyrics", true).toBool();
    lyricsBtn->setChecked(showLyrics);
    lyrics->setVisible(showLyrics);
    connect(lyricsBtn, &QPushButton::toggled, this, [lyrics](bool on) {
        lyrics->setVisible(on);
        Paths::settings()->setValue("ui/showLyrics", on);
    });
    // Double-click jumps inside the queue instead of replacing it.
    disconnect(m_nowPlaying.view, &QTableView::doubleClicked, nullptr, nullptr);
    connect(m_nowPlaying.view, &QTableView::doubleClicked, this, [this](const QModelIndex &i) { m_player->playAt(i.row()); });
    connect(save, &QPushButton::clicked, this, [this] { newPlaylist(m_player->queue()); });
    connect(clear, &QPushButton::clicked, m_player, &Player::clearQueue);
    connect(m_nowPlaying.view, &QWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        const QModelIndex i = m_nowPlaying.view->indexAt(p);
        if (!i.isValid()) return;
        QMenu menu;
        menu.addAction(QStringLiteral("Play"), this, [this, i] { m_player->playAt(i.row()); });
        menu.addAction(QStringLiteral("Remove from now playing"), this, [this, i] { m_player->removeAt(i.row()); });
        const Track t = m_nowPlaying.model->trackAt(i.row());
        menu.addAction(QStringLiteral("View in collection"), this, [this, t] { showAlbum(albumKey(t)); });
        menu.exec(m_nowPlaying.view->viewport()->mapToGlobal(p));
    });
    return w;
}

QWidget *CollectionPage::buildReports()
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *row = new QHBoxLayout;
    m_reportKind = new QComboBox;
    m_reportKind->addItems({QStringLiteral("Duplicate songs"), QStringLiteral("Missing album art"), QStringLiteral("Missing info")});
    row->addWidget(m_reportKind);
    m_reportInfo = new QLabel;
    m_reportInfo->setObjectName("dim");
    row->addWidget(m_reportInfo, 1);
    auto *reviewBtn = flatButton(QStringLiteral("review duplicates"));
    auto *artBtn = flatButton(QStringLiteral("find missing art now"));
    row->addWidget(reviewBtn);
    row->addWidget(artBtn);
    l->addLayout(row);
    connect(reviewBtn, &QPushButton::clicked, this, [this] {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const auto groups = Reports::duplicateReview(m_all, m_db);
        QApplication::restoreOverrideCursor();
        if (groups.isEmpty()) { emit statusMessage(QStringLiteral("No duplicates found.")); return; }
        DuplicateDialog dlg(m_lib, groups, this);
        dlg.exec();
    });
    connect(artBtn, &QPushButton::clicked, this, [this] {
        if (m_artFinder->isRunning()) { emit statusMessage(QStringLiteral("Already looking up album art.")); return; }
        emit statusMessage(QStringLiteral("Looking up album art on MusicBrainz…"));
        m_artFinder->start(true);
    });
    m_report = makeTable("report", true);
    configureColumns(m_report.view, "report", {TrackModel::Title, TrackModel::Artist, TrackModel::Album, TrackModel::Genre,
                                               TrackModel::Length, TrackModel::Bitrate, TrackModel::Path});
    l->addWidget(m_report.view, 1);
    connect(m_reportKind, &QComboBox::currentIndexChanged, this, &CollectionPage::refreshReports);
    const TableBundle tb = m_report;
    connect(m_report.view, &QWidget::customContextMenuRequested, this, [this, tb](const QPoint &p) { trackMenu(tb, p, false); });
    return w;
}

// ------------------------------------------------------------------ data
void CollectionPage::reload()
{
    m_all = m_db->allTracks();
    m_byId.clear();
    for (int i = 0; i < m_all.size(); ++i) m_byId.insert(m_all[i].id, i);
    qint64 ms = 0, bytes = 0;
    for (const Track &t : m_all) { ms += t.durationMs; bytes += t.sizeBytes; }
    m_summary->setText(QStringLiteral("%1 songs · %2 hours · %3   ").arg(m_all.size()).arg(ms / 3600000.0, 0, 'f', 1)
                           .arg(TrackModel::formatSize(bytes)));
    refreshBrowse(m_artists);
    refreshBrowse(m_albums);
    refreshBrowse(m_genres);
    m_songs.model->setTracks(m_all);
    refreshPlaylists();
    if (m_stack->currentIndex() == 6) refreshReports();
}

TrackList CollectionPage::tracksFor(const QString &kind, const QString &key) const
{
    if (kind == "playlist") return m_lib->playlistTracks(key.toLongLong(), m_all);
    TrackList out;
    for (const Track &t : m_all) {
        const bool hit = key.isEmpty() ||
            (kind == "artist" && t.displayAlbumArtist() == key) ||
            (kind == "album" && albumKey(t) == key) ||
            (kind == "genre" && t.displayGenre() == key);
        if (hit) out.push_back(t);
    }
    sortAlbumOrder(out);
    return out;
}

QIcon CollectionPage::albumIcon(const Track &t)
{
    const QString dir = QFileInfo(t.path).absolutePath();
    auto it = m_iconCache.constFind(dir);
    if (it != m_iconCache.cend()) return *it;
    QIcon icon;
    const QString art = TagIO::folderArtPath(dir);
    if (!art.isEmpty()) {
        QImage img(art);
        if (!img.isNull()) icon = QIcon(QPixmap::fromImage(img.scaled(88, 88, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)));
    }
    if (icon.isNull()) {
        // No cover: flat accent tile with the album's initial, so the list stays aligned.
        QPixmap pm(88, 88);
        pm.fill(palette().color(QPalette::Highlight).darker(160));
        QPainter p(&pm);
        p.setPen(Qt::white);
        QFont f = font();
        f.setPixelSize(40);
        f.setWeight(QFont::Light);
        p.setFont(f);
        p.drawText(pm.rect().adjusted(10, 0, 0, -6), Qt::AlignLeft | Qt::AlignBottom, t.displayAlbum().left(1).toLower());
        icon = QIcon(pm);
    }
    m_iconCache.insert(dir, icon);
    return icon;
}

void CollectionPage::refreshBrowse(Browse &b)
{
    const QString prev = b.list->currentKey();
    struct Entry { QString key, label, sort; int count = 0; Track sample; };
    QHash<QString, Entry> entries;
    for (const Track &t : m_all) {
        QString key, label;
        if (b.kind == "artist") key = label = t.displayAlbumArtist();
        else if (b.kind == "genre") key = label = t.displayGenre();
        else { key = albumKey(t); label = t.displayAlbum() + "\n" + t.displayAlbumArtist() + (t.year ? QStringLiteral(" · %1").arg(t.year) : QString()); }
        Entry &e = entries[key];
        if (e.count++ == 0) { e.key = key; e.label = label; e.sample = t; e.sort = (b.kind == "album" ? t.displayAlbum() : label).toLower(); }
    }
    QList<Entry> list = entries.values();
    std::sort(list.begin(), list.end(), [](const Entry &a, const Entry &b) { return a.sort < b.sort; });

    b.list->blockSignals(true);
    b.list->clear();
    auto *all = new QListWidgetItem(QStringLiteral("all (%1)").arg(list.size()));
    all->setData(MergeList::KeyRole, QString());
    all->setFlags(all->flags() & ~Qt::ItemIsDragEnabled & ~Qt::ItemIsDropEnabled);
    b.list->addItem(all);
    QListWidgetItem *select = all;
    for (const Entry &e : list) {
        auto *it = new QListWidgetItem(e.label);
        it->setData(MergeList::KeyRole, e.key);
        it->setToolTip(QStringLiteral("%1 songs").arg(e.count));
        if (b.kind == "album") it->setIcon(albumIcon(e.sample));
        b.list->addItem(it);
        if (e.key == prev) select = it;
    }
    b.list->setCurrentItem(select);
    b.list->blockSignals(false);
    applySearch();
    refreshBrowseTracks(b);
}

void CollectionPage::refreshBrowseTracks(Browse &b)
{
    const QString key = b.list->currentKey();
    const TrackList tracks = tracksFor(b.kind, key);
    b.table.model->setTracks(tracks);
    b.table.view->sortByColumn(-1, Qt::AscendingOrder);   // keep album order
    QString title = key.isEmpty() ? QStringLiteral("all") : key;
    if (b.kind == "album" && !key.isEmpty()) title = key.section(kSep, 1) + QStringLiteral("  ·  ") + key.section(kSep, 0, 0);
    qint64 ms = 0;
    for (const Track &t : tracks) ms += t.durationMs;
    b.header->setText(QStringLiteral("%1").arg(title.toLower()));
    b.header->setToolTip(QStringLiteral("%1 songs, %2 minutes").arg(tracks.size()).arg(ms / 60000));
}

void CollectionPage::refreshPlaylists()
{
    const QString prev = m_playlistList->currentKey();
    m_playlistList->blockSignals(true);
    m_playlistList->clear();
    QListWidgetItem *select = nullptr;
    for (const Playlist &p : m_db->playlists()) {
        auto *it = new QListWidgetItem((p.kind == "auto" ? QStringLiteral("⚙ ") : QString()) + p.name);
        it->setData(MergeList::KeyRole, QString::number(p.id));
        it->setToolTip(p.kind == "auto" ? QStringLiteral("Autoplaylist") : QStringLiteral("Playlist"));
        m_playlistList->addItem(it);
        if (QString::number(p.id) == prev || !select) select = it;
    }
    if (select) m_playlistList->setCurrentItem(select);
    m_playlistList->blockSignals(false);
    refreshPlaylistTracks();
}

void CollectionPage::refreshPlaylistTracks()
{
    auto pl = m_db->playlist(m_playlistList->currentKey().toLongLong());
    const bool isAuto = pl && pl->kind == "auto";
    m_staticButtons->setVisible(pl && !isAuto);
    m_autoButtons->setVisible(isAuto);
    if (!pl) {
        m_playlistHeader->setText(QStringLiteral("playlists"));
        m_playlistTable.model->setTracks({});
        return;
    }
    const TrackList tracks = m_lib->playlistTracks(pl->id, m_all);
    qint64 ms = 0;
    for (const Track &t : tracks) ms += t.durationMs;
    m_playlistHeader->setText(pl->name.toLower());
    m_playlistHeader->setToolTip(QStringLiteral("%1 songs, %2 minutes").arg(tracks.size()).arg(ms / 60000));
    m_playlistTable.model->setTracks(tracks);
}

void CollectionPage::refreshNowPlaying()
{
    m_nowPlaying.model->setTracks(m_player->queue());
}

void CollectionPage::refreshReports()
{
    TrackList list;
    switch (m_reportKind->currentIndex()) {
    case 0: {
        const auto groups = Reports::duplicates(m_all);
        for (const DuplicateGroup &g : groups) list += g.tracks;
        m_reportInfo->setText(QStringLiteral("  %1 songs with more than one copy. Use review duplicates to clean them up.").arg(groups.size()));
        break;
    }
    case 1:
        list = Reports::missingArt(m_all);
        m_reportInfo->setText(QStringLiteral("  %1 songs without album art. Right-click › Find album info or Paste album art.").arg(list.size()));
        break;
    default:
        list = Reports::missingTags(m_all);
        m_reportInfo->setText(QStringLiteral("  %1 songs missing a title, artist, album or genre").arg(list.size()));
        break;
    }
    m_report.model->setTracks(list);
    m_report.view->sortByColumn(-1, Qt::AscendingOrder);
}

void CollectionPage::applySearch()
{
    const QString text = m_search->text();
    for (TrackFilterProxy *p : {m_songs.proxy, m_artists.table.proxy, m_albums.table.proxy, m_genres.table.proxy,
                                m_playlistTable.proxy, m_report.proxy})
        p->setText(text);
    const QString lower = text.toLower();
    for (Browse *b : {&m_artists, &m_albums, &m_genres}) {
        for (int i = 1; i < b->list->count(); ++i) {
            QListWidgetItem *it = b->list->item(i);
            it->setHidden(!lower.isEmpty() && !it->text().toLower().contains(lower));
        }
    }
}

// ------------------------------------------------------------------ actions
TrackList CollectionPage::selectedTracks(const TableBundle &t) const
{
    TrackList out;
    QModelIndexList rows = t.view->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    for (const QModelIndex &r : rows) out.push_back(t.model->trackAt(t.proxy->mapToSource(r).row()));
    return out;
}

TrackList CollectionPage::visibleTracks(const TableBundle &t) const
{
    TrackList out;
    for (int r = 0; r < t.proxy->rowCount(); ++r) out.push_back(t.model->trackAt(t.proxy->mapToSource(t.proxy->index(r, 0)).row()));
    return out;
}

void CollectionPage::playFrom(const TableBundle &t, const QModelIndex &proxyIndex)
{
    m_player->playTracks(visibleTracks(t), proxyIndex.row());
}

void CollectionPage::addPlaylistMenu(QMenu *menu, const TrackList &tracks)
{
    QMenu *pm = menu->addMenu(QStringLiteral("Add to playlist"));
    QVector<qint64> ids;
    for (const Track &t : tracks) ids << t.id;
    pm->addAction(QStringLiteral("New playlist…"), this, [this, tracks] { newPlaylist(tracks); });
    pm->addSeparator();
    for (const Playlist &p : m_db->playlists()) {
        if (p.kind != "static") continue;
        pm->addAction(p.name, this, [this, p, ids] {
            m_db->appendToPlaylist(p.id, ids);
            emit statusMessage(QStringLiteral("Added %1 items to %2").arg(ids.size()).arg(p.name));
        });
    }
}

void CollectionPage::addDeviceMenus(QMenu *menu, const QString &kind, const QString &key, const TrackList &tracks)
{
    const auto devices = m_db->devices();
    if (devices.isEmpty()) {
        menu->addAction(QStringLiteral("Sync to device (add a device in sync first)"))->setEnabled(false);
        return;
    }
    QMenu *sm = menu->addMenu(QStringLiteral("Sync to device"));
    for (const DeviceProfile &d : devices) {
        sm->addAction(d.name, this, [this, d, kind, key, tracks] {
            if (kind == "track") {
                for (const Track &t : tracks) m_db->addSyncItem(d.id, {"track", t.path});
            } else {
                m_db->addSyncItem(d.id, {kind, key});
            }
            if (d.mode == "all") {
                emit statusMessage(QStringLiteral("%1 syncs your whole collection already. Change its sync mode to "
                                                  "\"items I choose\" to sync only selected items.").arg(d.name));
            } else {
                emit statusMessage(QStringLiteral("Added to %1").arg(d.name));
                if (d.mode == "manual") emit syncNowRequested(d.id);
            }
        });
    }
}

void CollectionPage::trackMenu(const TableBundle &t, const QPoint &pos, bool inPlaylist)
{
    const TrackList sel = selectedTracks(t);
    if (sel.isEmpty()) return;
    QMenu menu;
    menu.addAction(QStringLiteral("Play"), this, [this, sel] { m_player->playTracks(sel); });
    menu.addAction(QStringLiteral("Add to now playing"), this, [this, sel] { m_player->enqueue(sel); });
    addPlaylistMenu(&menu, sel);
    if (inPlaylist) {
        auto pl = m_db->playlist(m_playlistList->currentKey().toLongLong());
        if (pl && pl->kind == "static") {
            menu.addAction(QStringLiteral("Remove from playlist"), this, [this, pl, t] {
                QSet<int> rows;
                for (const QModelIndex &r : t.view->selectionModel()->selectedRows()) rows.insert(t.proxy->mapToSource(r).row());
                QVector<qint64> items = m_db->playlistItems(pl->id), out;
                for (int i = 0; i < items.size(); ++i) if (!rows.contains(i)) out << items[i];
                m_db->setPlaylistItems(pl->id, out);
            });
        }
    }
    menu.addSeparator();
    menu.addAction(QStringLiteral("Edit info…"), this, [this, sel] { editInfo(sel); });
    QMenu *rm = menu.addMenu(QStringLiteral("Rate"));
    rm->addAction(QStringLiteral("♥  Like"), this, [this, sel] { rate(sel, Like); });
    rm->addAction(QStringLiteral("✕  Don't like"), this, [this, sel] { rate(sel, Dislike); });
    rm->addAction(QStringLiteral("Unrated"), this, [this, sel] { rate(sel, Unrated); });
    const Track first = sel.first();
    menu.addAction(QStringLiteral("Find album info…"), this, [this, first] {
        AlbumLookupDialog dlg(m_lib, tracksFor("album", albumKey(first)), this);
        dlg.exec();
    });
    menu.addAction(QStringLiteral("Paste album art"), this, [this, first] { pasteArt(albumKey(first)); });
    QMenu *pinMenu = menu.addMenu(QStringLiteral("Pin to Quickplay"));
    pinMenu->addAction(QStringLiteral("Album: %1").arg(first.displayAlbum()), this, [this, first] {
        pin("album", albumKey(first), first.displayAlbum()); });
    pinMenu->addAction(QStringLiteral("Artist: %1").arg(first.displayAlbumArtist()), this, [this, first] {
        pin("artist", first.displayAlbumArtist(), first.displayAlbumArtist()); });
    pinMenu->addAction(QStringLiteral("Genre: %1").arg(first.displayGenre()), this, [this, first] {
        pin("genre", first.displayGenre(), first.displayGenre()); });
    addDeviceMenus(&menu, "track", {}, sel);
    menu.addSeparator();
    menu.addAction(QStringLiteral("View album"), this, [this, first] { showAlbum(albumKey(first)); });
    menu.addAction(QStringLiteral("Remove from collection…"), this, [this, sel] { removeFromCollection(sel); });
    menu.exec(t.view->viewport()->mapToGlobal(pos));
}

void CollectionPage::itemMenu(Browse &b, const QPoint &pos)
{
    QListWidgetItem *it = b.list->itemAt(pos);
    if (!it) return;
    const QString key = it->data(MergeList::KeyRole).toString();
    const QString label = it->text().section('\n', 0, 0);
    const TrackList tracks = tracksFor(b.kind, key);
    QMenu menu;
    menu.addAction(QStringLiteral("Play"), this, [this, tracks] { m_player->playTracks(tracks); });
    menu.addAction(QStringLiteral("Add to now playing"), this, [this, tracks] { m_player->enqueue(tracks); });
    addPlaylistMenu(&menu, tracks);
    if (key.isEmpty()) { menu.exec(b.list->mapToGlobal(pos)); return; }
    menu.addAction(QStringLiteral("Pin to Quickplay"), this, [this, b, key, label] { pin(b.kind, key, label); });
    menu.addAction(QStringLiteral("Edit info…"), this, [this, tracks] { editInfo(tracks); });
    if (b.kind == "album") {
        menu.addAction(QStringLiteral("Find album info…"), this, [this, tracks] { AlbumLookupDialog dlg(m_lib, tracks, this); dlg.exec(); });
        menu.addAction(QStringLiteral("Paste album art"), this, [this, key] { pasteArt(key); });
    }
    QMenu *mm = menu.addMenu(QStringLiteral("Merge into"));
    int n = 0;
    for (int i = 1; i < b.list->count() && n < 40; ++i) {
        QListWidgetItem *other = b.list->item(i);
        if (other == it || other->isHidden()) continue;
        const QString ok = other->data(MergeList::KeyRole).toString();
        const QString ol = other->text().section('\n', 0, 0);
        mm->addAction(other->text().replace('\n', QStringLiteral(" — ")), this, [this, b, key, label, ok, ol] {
            merge(b.kind, key, label, ok, ol); });
        ++n;
    }
    if (n == 40) mm->addAction(QStringLiteral("(search to narrow the list, or drag onto the target)"))->setEnabled(false);
    addDeviceMenus(&menu, b.kind, key, tracks);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Remove from collection…"), this, [this, tracks] { removeFromCollection(tracks); });
    menu.exec(b.list->mapToGlobal(pos));
}

void CollectionPage::editInfo(const TrackList &tracks)
{
    if (tracks.isEmpty()) return;
    TagEditorDialog dlg(tracks, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QVector<qint64> ids;
    for (const Track &t : tracks) ids << t.id;
    QStringList errors;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_lib->editTags(ids, dlg.edit(), &errors);
    QApplication::restoreOverrideCursor();
    m_player->refreshCurrent();
    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Some files weren't updated"), errors.join('\n').left(1500));
}

void CollectionPage::rate(const TrackList &tracks, int rating)
{
    m_db->begin();
    for (const Track &t : tracks) m_db->setRating(t.id, rating);
    m_db->commit();
    m_player->refreshCurrent();
}

void CollectionPage::pin(const QString &type, const QString &key, const QString &label)
{
    m_db->addPin({0, type, key, label, 0});
    emit statusMessage(QStringLiteral("Pinned %1 to Quickplay").arg(label));
}

void CollectionPage::pasteArt(const QString &key)
{
    QString err;
    const QImage img = QApplication::clipboard()->image();
    if (!m_lib->pasteAlbumArt(key.section(kSep, 0, 0), key.section(kSep, 1), img, &err))
        QMessageBox::information(this, QStringLiteral("Paste album art"), err.isEmpty() ? QStringLiteral("Couldn't save the album art.") : err);
    else {
        m_iconCache.clear();
        emit statusMessage(QStringLiteral("Album art updated"));
    }
}

void CollectionPage::merge(const QString &kind, const QString &fromKey, const QString &fromLabel,
                           const QString &toKey, const QString &toLabel)
{
    if (fromKey.isEmpty() || toKey.isEmpty() || fromKey == toKey) return;
    const TrackList affected = tracksFor(kind, fromKey);
    const auto answer = QMessageBox::question(this, QStringLiteral("Merge"),
        QStringLiteral("Rename \"%1\" to \"%2\"? This changes the info in %3 files.")
            .arg(fromLabel, toLabel).arg(affected.size()));
    if (answer != QMessageBox::Yes) return;
    QStringList errors;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    if (kind == "album") {
        TagEdit e;
        e.album = toKey.section(kSep, 1);
        e.albumArtist = toKey.section(kSep, 0, 0);
        QVector<qint64> ids;
        for (const Track &t : affected) ids << t.id;
        m_lib->editTags(ids, e, &errors);
    } else {
        m_lib->merge(kind == "artist" ? "albumArtist" : kind, fromKey, toKey, &errors);
    }
    QApplication::restoreOverrideCursor();
    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Some files weren't updated"), errors.join('\n').left(1500));
}

void CollectionPage::removeFromCollection(const TrackList &tracks)
{
    const bool deleteFiles = Paths::settings()->value("library/deleteFiles", false).toBool();
    const QString what = tracks.size() == 1 ? QStringLiteral("\"%1\"").arg(tracks.first().displayTitle())
                                            : QStringLiteral("these %1 items").arg(tracks.size());
    const QString msg = deleteFiles
        ? QStringLiteral("Remove %1 from the collection and delete the files from your computer?").arg(what)
        : QStringLiteral("Remove %1 from the collection? The files stay on your computer, but they come back on "
                         "the next scan if they're still in a monitored folder.").arg(what);
    if (QMessageBox::question(this, QStringLiteral("Remove"), msg) != QMessageBox::Yes) return;
    QVector<qint64> ids;
    for (const Track &t : tracks) ids << t.id;
    m_lib->removeFromCollection(ids, deleteFiles);
}

void CollectionPage::newPlaylist(const TrackList &initial)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New playlist"), QStringLiteral("Playlist name"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const qint64 id = m_db->createPlaylist(name);
    if (id < 0) {
        QMessageBox::warning(this, QStringLiteral("New playlist"),
                             QStringLiteral("You already have a playlist called \"%1\". Choose a different name.").arg(name));
        return;
    }
    QVector<qint64> ids;
    for (const Track &t : initial) ids << t.id;
    if (!ids.isEmpty()) m_db->setPlaylistItems(id, ids);
    showPlaylist(id);
}

void CollectionPage::newAutoPlaylist()
{
    AutoPlaylistDialog dlg(QString(), AutoPlaylistSpec{}, m_all, this);
    while (dlg.exec() == QDialog::Accepted) {
        const qint64 id = m_db->createPlaylist(dlg.name(), "auto", dlg.spec().toJsonString());
        if (id >= 0) { showPlaylist(id); return; }
        QMessageBox::warning(this, QStringLiteral("New autoplaylist"),
                             QStringLiteral("You already have a playlist called \"%1\". Choose a different name.").arg(dlg.name()));
    }
}

void CollectionPage::editAutoPlaylist(qint64 id)
{
    auto pl = m_db->playlist(id);
    if (!pl || pl->kind != "auto") return;
    AutoPlaylistDialog dlg(pl->name, AutoPlaylistSpec::fromJsonString(pl->rulesJson), m_all, this);
    if (dlg.exec() != QDialog::Accepted) return;
    if (dlg.name() != pl->name && !m_db->renamePlaylist(id, dlg.name()))
        QMessageBox::warning(this, QStringLiteral("Autoplaylist"), QStringLiteral("That name is taken; the old name was kept."));
    m_db->updatePlaylistRules(id, dlg.spec().toJsonString());
}

void CollectionPage::importPlaylist()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import playlists"), QString(),
                                                            PlaylistIO::fileFilter());
    int imported = 0, missing = 0;
    for (const QString &f : files) {
        QString err;
        auto pl = PlaylistIO::read(f, &err);
        if (!pl) { QMessageBox::warning(this, QStringLiteral("Import"), err); continue; }
        QHash<QString, qint64> byPath;
        for (const Track &t : m_all) byPath.insert(QFileInfo(t.path).canonicalFilePath(), t.id);
        QVector<qint64> ids;
        for (const PlaylistEntry &e : pl->entries) {
            const qint64 id = byPath.value(QFileInfo(e.path).canonicalFilePath(), 0);
            if (id) ids << id; else ++missing;
        }
        QString name = pl->name;
        for (int n = 2; m_db->playlistByName(name); ++n) name = QStringLiteral("%1 (%2)").arg(pl->name).arg(n);
        const qint64 id = m_db->createPlaylist(name);
        m_db->setPlaylistItems(id, ids);
        ++imported;
    }
    if (imported)
        emit statusMessage(QStringLiteral("Imported %1 playlists%2").arg(imported)
            .arg(missing ? QStringLiteral(" (%1 songs weren't in your collection and were skipped)").arg(missing) : QString()));
}

void CollectionPage::exportPlaylist(qint64 id)
{
    auto pl = m_db->playlist(id);
    if (!pl) return;
    const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Export playlist"), pl->name + ".m3u8",
                                                      PlaylistIO::fileFilter());
    if (file.isEmpty()) return;
    PlaylistFile out{pl->name, {}};
    for (const Track &t : m_lib->playlistTracks(id, m_all))
        out.entries.push_back({t.path, t.displayTitle(), t.displayArtist(), t.durationMs});
    QString err;
    if (!PlaylistIO::write(file, out, false, &err)) QMessageBox::warning(this, QStringLiteral("Export"), err);
    else emit statusMessage(QStringLiteral("Saved %1").arg(QFileInfo(file).fileName()));
}

// ------------------------------------------------------------------ navigation
void CollectionPage::focusSearch() { m_search->setFocus(); m_search->selectAll(); }

void CollectionPage::showView(const QString &name)
{
    const int i = int(m_viewNames.indexOf(name));
    if (i < 0) return;
    m_pivots->button(i)->setChecked(true);
    m_stack->setCurrentIndex(i);
    if (name == QLatin1String("reports")) refreshReports();
}

void CollectionPage::showPlaylist(qint64 id)
{
    showView("playlists");
    refreshPlaylists();
    for (int i = 0; i < m_playlistList->count(); ++i)
        if (m_playlistList->item(i)->data(MergeList::KeyRole).toLongLong() == id) m_playlistList->setCurrentRow(i);
}

static void selectKey(QListWidget *list, const QString &key)
{
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(MergeList::KeyRole).toString() == key) { list->setCurrentRow(i); list->scrollToItem(list->item(i)); return; }
}

void CollectionPage::showArtist(const QString &name) { m_search->clear(); showView("artists"); selectKey(m_artists.list, name); }
void CollectionPage::showAlbum(const QString &key) { m_search->clear(); showView("albums"); selectKey(m_albums.list, key); }
void CollectionPage::showGenre(const QString &name) { m_search->clear(); showView("genres"); selectKey(m_genres.list, name); }

} // namespace halo
