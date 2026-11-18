#pragma once
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

namespace halo {

class Library;
class Player;

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    SettingsPage(Library *library, Player *player, QWidget *parent = nullptr);
signals:
    void themeChanged();
    void statusMessage(const QString &text);
private:
    void refreshFolders();
    void refreshCache();
    Library *m_lib;
    Player *m_player;
    QListWidget *m_folders;
    QLabel *m_cacheLabel;
    QLabel *m_ffmpegLabel;
};

} // namespace halo
