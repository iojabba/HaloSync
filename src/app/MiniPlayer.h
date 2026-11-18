#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

class QMouseEvent;
class QCloseEvent;

namespace halo {

class Player;

// Compact always-on-top player (SPEC §2.1 mini mode).
class MiniPlayer : public QWidget {
    Q_OBJECT
public:
    MiniPlayer(Player *player, QWidget *parent = nullptr);
signals:
    void restoreRequested();
protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
private:
    void refresh();
    Player *m_player;
    QLabel *m_title, *m_artist;
    QPushButton *m_play;
    QPoint m_dragOffset;
};

} // namespace halo
