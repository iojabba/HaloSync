#include "MiniPlayer.h"
#include "Player.h"
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace halo {

MiniPlayer::MiniPlayer(Player *player, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint), m_player(player)
{
    setWindowTitle(QStringLiteral("Halo Sync"));
    setAttribute(Qt::WA_StyledBackground);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 10, 10, 10);
    auto *text = new QVBoxLayout;
    m_title = new QLabel;
    m_title->setObjectName("nowTitle");
    m_artist = new QLabel;
    m_artist->setObjectName("nowSub");
    text->addWidget(m_title);
    text->addWidget(m_artist);
    lay->addLayout(text, 1);
    auto mk = [&](const QString &glyph, const QString &tip) {
        auto *b = new QPushButton(glyph);
        b->setObjectName("transport");
        b->setToolTip(tip);
        lay->addWidget(b);
        return b;
    };
    auto *prev = mk("⏮", "Previous");
    m_play = mk("▶", "Play/Pause");
    auto *next = mk("⏭", "Next");
    auto *restore = mk("⤢", "Back to full view");
    connect(prev, &QPushButton::clicked, m_player, &Player::previous);
    connect(m_play, &QPushButton::clicked, m_player, &Player::togglePlayPause);
    connect(next, &QPushButton::clicked, m_player, &Player::next);
    connect(restore, &QPushButton::clicked, this, &MiniPlayer::restoreRequested);
    connect(m_player, &Player::currentChanged, this, &MiniPlayer::refresh);
    connect(m_player, &Player::playingChanged, this, &MiniPlayer::refresh);
    setFixedSize(380, 74);
    refresh();
}

void MiniPlayer::refresh()
{
    const auto t = m_player->current();
    m_title->setText(t ? t->displayTitle() : QStringLiteral("nothing playing"));
    m_artist->setText(t ? t->displayArtist() : QString());
    m_play->setText(m_player->isPlaying() ? QStringLiteral("⏸") : QStringLiteral("▶"));
}

void MiniPlayer::mousePressEvent(QMouseEvent *e) { m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft(); }
void MiniPlayer::mouseMoveEvent(QMouseEvent *e) { if (e->buttons() & Qt::LeftButton) move(e->globalPosition().toPoint() - m_dragOffset); }
void MiniPlayer::closeEvent(QCloseEvent *e) { e->ignore(); emit restoreRequested(); }

} // namespace halo
