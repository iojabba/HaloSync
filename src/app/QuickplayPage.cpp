#include "QuickplayPage.h"
#include "CollectionPage.h"
#include "Library.h"
#include "Player.h"
#include "TagIO.h"
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QVBoxLayout>

namespace halo {

namespace {
constexpr int kKindRole = Qt::UserRole + 1, kKeyRole = Qt::UserRole + 2, kPinIdRole = Qt::UserRole + 3;

// Tile image: album art when there is some, else a flat accent square with initials.
QIcon tileIcon(const QString &artPath, const QString &label, const QColor &accent)
{
    QPixmap pm(150, 150);
    QImage img;
    if (!artPath.isEmpty()) img.load(artPath);
    if (!img.isNull()) {
        pm = QPixmap::fromImage(img.scaled(150, 150, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).copy(0, 0, 150, 150));
    } else {
        pm.fill(accent);
        QPainter p(&pm);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(44);
        f.setWeight(QFont::Light);
        p.setFont(f);
        QString initials;
        for (const QString &w : label.split(' ', Qt::SkipEmptyParts).mid(0, 2)) initials += w.at(0).toLower();
        p.drawText(pm.rect().adjusted(12, 0, 0, -10), Qt::AlignLeft | Qt::AlignBottom, initials);
    }
    return QIcon(pm);
}
} // namespace

QuickplayPage::QuickplayPage(Library *library, Player *player, CollectionPage *collection, QWidget *parent)
    : QWidget(parent), m_lib(library), m_db(library->db()), m_player(player), m_collection(collection)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *h1 = new QLabel(QStringLiteral("PINNED"));
    h1->setObjectName("h2");
    lay->addWidget(h1);
    m_empty = new QLabel(QStringLiteral("Nothing pinned yet. Right-click an album, artist, genre or playlist "
                                        "in your collection and choose Pin to Quickplay."));
    m_empty->setObjectName("dim");
    m_empty->setWordWrap(true);
    lay->addWidget(m_empty);
    m_pins = new QListWidget;
    m_pins->setViewMode(QListView::IconMode);
    m_pins->setIconSize(QSize(150, 150));
    m_pins->setGridSize(QSize(170, 200));
    m_pins->setResizeMode(QListView::Adjust);
    m_pins->setMovement(QListView::Static);
    m_pins->setWordWrap(true);
    m_pins->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pins->setMinimumHeight(220);
    lay->addWidget(m_pins, 3);
    auto *h2 = new QLabel(QStringLiteral("RECENTLY PLAYED"));
    h2->setObjectName("h2");
    lay->addWidget(h2);
    m_recent = new QListWidget;
    lay->addWidget(m_recent, 2);

    connect(m_pins, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        const QString kind = it->data(kKindRole).toString();
        m_player->playTracks(m_collection->tracksFor(kind, it->data(kKeyRole).toString()));
    });
    connect(m_pins, &QWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        QListWidgetItem *it = m_pins->itemAt(p);
        if (!it) return;
        const QString kind = it->data(kKindRole).toString(), key = it->data(kKeyRole).toString();
        QMenu menu;
        menu.addAction(QStringLiteral("Play"), this, [this, kind, key] { m_player->playTracks(m_collection->tracksFor(kind, key)); });
        menu.addAction(QStringLiteral("Shuffle"), this, [this, kind, key] {
            m_player->setShuffle(true);
            m_player->playTracks(m_collection->tracksFor(kind, key));
        });
        menu.addAction(QStringLiteral("Show in collection"), this, [this, kind, key] {
            if (kind == "artist") m_collection->showArtist(key);
            else if (kind == "album") m_collection->showAlbum(key);
            else if (kind == "genre") m_collection->showGenre(key);
            else m_collection->showPlaylist(key.toLongLong());
            emit openCollection();
        });
        menu.addAction(QStringLiteral("Unpin"), this, [this, it] { m_db->removePin(it->data(kPinIdRole).toLongLong()); });
        menu.exec(m_pins->mapToGlobal(p));
    });
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        TrackList list;
        for (int i = m_recent->row(it); i < m_recent->count(); ++i)
            if (auto t = m_db->track(m_recent->item(i)->data(kKeyRole).toLongLong())) list.push_back(*t);
        m_player->playTracks(list);
    });
    connect(m_db, &Database::pinsChanged, this, &QuickplayPage::refresh);
    connect(m_db, &Database::playlistsChanged, this, &QuickplayPage::refresh);
    refresh();
}

void QuickplayPage::refresh()
{
    const QColor accent = palette().color(QPalette::Highlight);
    m_pins->clear();
    const auto pins = m_db->pins();
    m_empty->setVisible(pins.isEmpty());
    const TrackList &all = m_collection->allTracks();
    for (const Pin &p : pins) {
        QString art;
        if (p.type == "album" || p.type == "artist") {
            for (const Track &t : all) {
                const bool match = p.type == "artist" ? t.displayAlbumArtist() == p.key
                                                      : t.displayAlbumArtist() + QChar(0x1f) + t.displayAlbum() == p.key;
                if (match) { art = TagIO::folderArtPath(QFileInfo(t.path).absolutePath()); if (!art.isEmpty()) break; }
            }
        }
        QString label = p.label;
        if (p.type == "playlist") if (auto pl = m_db->playlist(p.key.toLongLong())) label = pl->name;
        auto *it = new QListWidgetItem(tileIcon(art, label, accent), label.toLower() + "\n" + p.type);
        it->setData(kKindRole, p.type);
        it->setData(kKeyRole, p.key);
        it->setData(kPinIdRole, p.id);
        m_pins->addItem(it);
    }
    m_recent->clear();
    for (qint64 id : m_db->recentTrackIds(30)) {
        auto t = m_db->track(id);
        if (!t) continue;
        auto *it = new QListWidgetItem(QStringLiteral("%1   ·   %2   ·   %3").arg(t->displayTitle(), t->displayArtist(), t->displayAlbum()));
        it->setData(kKeyRole, id);
        m_recent->addItem(it);
    }
}

} // namespace halo
