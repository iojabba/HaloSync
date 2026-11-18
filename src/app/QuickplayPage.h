#pragma once
#include "Database.h"
#include <QWidget>

class QListWidget;
class QLabel;

namespace halo {

class CollectionPage;
class Library;
class Player;

// Pinned items + recently played (SPEC §2.6).
class QuickplayPage : public QWidget {
    Q_OBJECT
public:
    QuickplayPage(Library *library, Player *player, CollectionPage *collection, QWidget *parent = nullptr);
    void refresh();
signals:
    void openCollection();
private:
    Library *m_lib;
    Database *m_db;
    Player *m_player;
    CollectionPage *m_collection;
    QListWidget *m_pins;
    QListWidget *m_recent;
    QLabel *m_empty;
};

} // namespace halo
