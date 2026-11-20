#pragma once
#include "Lyrics.h"
#include <QWidget>

class QLabel;
class QListWidget;

namespace halo {

class Player;

// Lyrics beside the Now Playing queue; synced (LRC) lyrics follow the song.
class LyricsPanel : public QWidget {
    Q_OBJECT
public:
    explicit LyricsPanel(Player *player, QWidget *parent = nullptr);
private:
    void load();
    void show(qint64 trackId, const Lyrics &lyrics);
    void follow(qint64 ms);
    Player *m_player;
    LyricsService m_service;
    Lyrics m_lyrics;
    qint64 m_trackId = -2;   // -1 = nothing playing
    int m_line = -1;
    QLabel *m_status;
    QListWidget *m_list;
};

} // namespace halo
