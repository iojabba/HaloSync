#pragma once
#include "Discover.h"
#include <QIcon>
#include <QTimer>
#include <QWidget>
#include <functional>

class QVBoxLayout;

namespace halo {

class Database;
class Player;

// Discover: shelves of suggestions from the listening history (see Discover.h).
class DiscoverView : public QWidget {
    Q_OBJECT
public:
    using IconFor = std::function<QIcon(const Track &)>;
    DiscoverView(Database *db, Player *player, IconFor icons, QWidget *parent = nullptr);
    void refresh();
protected:
    void showEvent(QShowEvent *e) override;
private:
    Database *m_db;
    Player *m_player;
    IconFor m_icons;
    QVBoxLayout *m_shelves;
    QTimer m_debounce;
    bool m_dirty = true;
};

} // namespace halo
