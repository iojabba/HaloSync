#pragma once
#include "Library.h"
#include "MusicBrainz.h"
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace halo {

// "Find album info" (SPEC §2.4) backed by MusicBrainz + Cover Art Archive.
// Search → pick a release → review the song matches → confirm.
class AlbumLookupDialog : public QDialog {
    Q_OBJECT
public:
    AlbumLookupDialog(Library *library, const TrackList &albumTracks, QWidget *parent = nullptr);

private:
    void search();
    void showRelease(const MbReleaseDetail &d);
    void apply();
    Library *m_library;
    TrackList m_tracks;
    MusicBrainz m_mb;
    MbReleaseDetail m_release;
    QStackedWidget *m_stack;
    QLineEdit *m_artist, *m_album;
    QListWidget *m_results;
    QTableWidget *m_matches;
    QCheckBox *m_cover;
    QLabel *m_status;
    QPushButton *m_apply;
    QVector<MbRelease> m_found;
};

} // namespace halo
