#pragma once
#include "Radio.h"
#include <QNetworkAccessManager>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;

namespace halo {

class Database;
class Player;

// Internet radio: favourites, Radio Browser search, stream URLs and station files.
class RadioView : public QWidget {
    Q_OBJECT
public:
    RadioView(Database *db, Player *player, QWidget *parent = nullptr);
signals:
    void statusMessage(const QString &text);
private:
    void refreshFavourites();
    void showResults(const QVector<RadioStation> &stations);
    void play(const RadioStation &s);
    void addUrl();
    void openStationFile();
    void stationMenu(const RadioStation &s, const QPoint &globalPos, bool favourite);
    Database *m_db;
    Player *m_player;
    RadioBrowser m_browser;
    QNetworkAccessManager m_net;
    QVector<RadioStation> m_results, m_favourites;
    QLineEdit *m_query;
    QComboBox *m_tag;
    QListWidget *m_favList;
    QTableWidget *m_table;
    QLabel *m_info, *m_now;
};

} // namespace halo
