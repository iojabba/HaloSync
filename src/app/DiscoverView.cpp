#include "DiscoverView.h"
#include "Database.h"
#include "Player.h"
#include <QDateTime>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QScrollArea>
#include <QVBoxLayout>

namespace halo {

DiscoverView::DiscoverView(Database *db, Player *player, IconFor icons, QWidget *parent)
    : QWidget(parent), m_db(db), m_player(player), m_icons(std::move(icons))
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *h = new QLabel(QStringLiteral("discover"));
    h->setObjectName("h1");
    outer->addWidget(h);
    auto *note = new QLabel(QStringLiteral("Suggestions from your own collection and listening history. Double-click to play."));
    note->setObjectName("dim");
    outer->addWidget(note);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget;
    m_shelves = new QVBoxLayout(inner);
    m_shelves->setContentsMargins(0, 8, 0, 0);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(1500);
    connect(&m_debounce, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); else m_dirty = true; });
    connect(m_db, &Database::tracksChanged, &m_debounce, qOverload<>(&QTimer::start));
}

void DiscoverView::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_dirty) refresh();
}

void DiscoverView::refresh()
{
    m_dirty = false;
    while (QLayoutItem *it = m_shelves->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    const auto shelves = Discover::shelves(m_db->allTracks(), m_db->history(), QDateTime::currentSecsSinceEpoch());
    if (shelves.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("Play some music and come back: Discover learns from what you listen to."));
        empty->setObjectName("dim");
        m_shelves->addWidget(empty);
    }
    for (const DiscoverShelf &s : shelves) {
        auto *title = new QLabel(s.title.toUpper());
        title->setObjectName("h2");
        m_shelves->addWidget(title);
        auto *sub = new QLabel(s.subtitle);
        sub->setObjectName("dim");
        m_shelves->addWidget(sub);
        auto *row = new QListWidget;
        row->setViewMode(QListView::IconMode);
        row->setFlow(QListView::LeftToRight);
        row->setWrapping(false);
        row->setMovement(QListView::Static);
        row->setIconSize(QSize(88, 88));
        row->setGridSize(QSize(132, 150));
        row->setFixedHeight(172);
        row->setWordWrap(true);
        row->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        row->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        const QVector<DiscoverItem> items = s.items;
        for (const DiscoverItem &item : items) {
            auto *it = new QListWidgetItem(m_icons(item.tracks.front()), item.label + '\n' + item.sublabel);
            it->setToolTip(item.label + QStringLiteral(" · ") + item.sublabel);
            it->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
            row->addItem(it);
        }
        connect(row, &QListWidget::itemDoubleClicked, this, [this, row, items](QListWidgetItem *it) {
            m_player->playTracks(items.value(row->row(it)).tracks);
        });
        connect(row, &QWidget::customContextMenuRequested, this, [this, row, items](const QPoint &p) {
            QListWidgetItem *it = row->itemAt(p);
            if (!it) return;
            const TrackList tracks = items.value(row->row(it)).tracks;
            QMenu menu;
            menu.addAction(QStringLiteral("Play"), this, [this, tracks] { m_player->playTracks(tracks); });
            menu.addAction(QStringLiteral("Add to now playing"), this, [this, tracks] { m_player->enqueue(tracks); });
            menu.exec(row->viewport()->mapToGlobal(p));
        });
        m_shelves->addWidget(row);
        m_shelves->addSpacing(8);
    }
    m_shelves->addStretch();
}

} // namespace halo
