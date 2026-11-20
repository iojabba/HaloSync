#include "LyricsPanel.h"
#include "Player.h"
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace halo {

LyricsPanel::LyricsPanel(Player *player, QWidget *parent) : QWidget(parent), m_player(player)
{
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(12, 0, 0, 0);
    auto *h = new QLabel(QStringLiteral("LYRICS"));
    h->setObjectName("h2");
    l->addWidget(h);
    m_status = new QLabel;
    m_status->setObjectName("dim");
    m_status->setWordWrap(true);
    l->addWidget(m_status);
    m_list = new QListWidget;
    m_list->setObjectName("lyrics");
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setWordWrap(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    l->addWidget(m_list, 1);
    connect(&m_service, &LyricsService::ready, this, &LyricsPanel::show);
    connect(m_player, &Player::currentChanged, this, &LyricsPanel::load);
    connect(m_player, &Player::positionChanged, this, &LyricsPanel::follow);
    load();
}

void LyricsPanel::load()
{
    const auto t = m_player->current();
    const qint64 id = t ? t->id : -1;
    if (t && m_player->isStream()) { m_trackId = -1; m_list->clear(); m_status->setText(QStringLiteral("No lyrics for radio.")); return; }
    if (id == m_trackId) return;
    m_trackId = id;
    m_lyrics = {};
    m_line = -1;
    m_list->clear();
    if (!t) { m_status->setText(QStringLiteral("Nothing is playing.")); return; }
    if (!LyricsService::enabled()) { m_status->setText(QStringLiteral("Lyrics are turned off in Settings.")); return; }
    m_status->setText(QStringLiteral("Looking for lyrics…"));
    m_service.fetch(*t);
}

void LyricsPanel::show(qint64 trackId, const Lyrics &lyrics)
{
    if (trackId != m_trackId) return;
    m_lyrics = lyrics;
    m_line = -1;
    m_list->clear();
    if (lyrics.instrumental) { m_status->setText(QStringLiteral("This song is an instrumental.")); return; }
    if (lyrics.lines.isEmpty()) { m_status->setText(QStringLiteral("No lyrics found for this song.")); return; }
    const QString from = lyrics.source == QLatin1String("lrclib") || lyrics.source == QLatin1String("cache")
                             ? QStringLiteral("from LRCLIB") : QStringLiteral("from the song file");
    m_status->setText(lyrics.synced ? QStringLiteral("Synced lyrics %1").arg(from) : QStringLiteral("Lyrics %1").arg(from));
    for (const LyricLine &line : lyrics.lines) {
        auto *it = new QListWidgetItem(line.text.isEmpty() ? QStringLiteral(" ") : line.text);
        if (lyrics.synced) it->setForeground(palette().color(QPalette::PlaceholderText));
        m_list->addItem(it);
    }
    follow(m_player->position());
}

void LyricsPanel::follow(qint64 ms)
{
    if (!m_lyrics.synced || m_list->count() == 0) return;
    const int line = m_lyrics.lineAt(ms);
    if (line == m_line) return;
    if (QListWidgetItem *old = m_list->item(m_line)) {
        old->setForeground(palette().color(QPalette::PlaceholderText));
        old->setFont(font());
    }
    m_line = line;
    if (QListWidgetItem *cur = m_list->item(line)) {
        cur->setForeground(palette().color(QPalette::Highlight));
        QFont f = font();
        f.setBold(true);
        cur->setFont(f);
        m_list->scrollToItem(cur, QAbstractItemView::PositionAtCenter);
    }
}

} // namespace halo
