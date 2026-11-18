#include "AlbumLookupDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace halo {

AlbumLookupDialog::AlbumLookupDialog(Library *library, const TrackList &albumTracks, QWidget *parent)
    : QDialog(parent), m_library(library), m_tracks(albumTracks)
{
    std::sort(m_tracks.begin(), m_tracks.end(), [](const Track &a, const Track &b) {
        if (a.discNo != b.discNo) return a.discNo < b.discNo;
        if (a.trackNo != b.trackNo) return a.trackNo < b.trackNo;
        return a.path < b.path;
    });
    setWindowTitle(QStringLiteral("Find album info"));
    auto *lay = new QVBoxLayout(this);
    auto *h = new QLabel(QStringLiteral("find album info"));
    h->setObjectName("h1");
    lay->addWidget(h);
    m_stack = new QStackedWidget;
    lay->addWidget(m_stack, 1);

    // Page 1: search
    auto *p1 = new QWidget;
    auto *l1 = new QVBoxLayout(p1);
    auto *form = new QFormLayout;
    const Track first = m_tracks.value(0);
    m_artist = new QLineEdit(first.displayAlbumArtist() == "Unknown Artist" ? QString() : first.displayAlbumArtist());
    m_album = new QLineEdit(first.album);
    form->addRow(QStringLiteral("Artist"), m_artist);
    form->addRow(QStringLiteral("Album"), m_album);
    l1->addLayout(form);
    auto *go = new QPushButton(QStringLiteral("search"));
    go->setObjectName("accent");
    l1->addWidget(go, 0, Qt::AlignLeft);
    m_results = new QListWidget;
    l1->addWidget(m_results, 1);
    m_stack->addWidget(p1);

    // Page 2: review matches
    auto *p2 = new QWidget;
    auto *l2 = new QVBoxLayout(p2);
    m_matches = new QTableWidget(0, 3);
    m_matches->setHorizontalHeaderLabels({QStringLiteral("YOUR SONG"), QStringLiteral("NEW TITLE"), QStringLiteral("NEW ARTIST")});
    m_matches->horizontalHeader()->setStretchLastSection(true);
    m_matches->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_matches->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_matches->verticalHeader()->hide();
    l2->addWidget(m_matches, 1);
    m_cover = new QCheckBox(QStringLiteral("Also download the album cover (saved as folder.jpg)"));
    m_cover->setChecked(true);
    l2->addWidget(m_cover);
    auto *back = new QPushButton(QStringLiteral("back"));
    back->setObjectName("flat");
    l2->addWidget(back, 0, Qt::AlignLeft);
    m_stack->addWidget(p2);

    m_status = new QLabel;
    m_status->setObjectName("dim");
    m_status->setWordWrap(true);
    lay->addWidget(m_status);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_apply = bb->addButton(QStringLiteral("Apply changes"), QDialogButtonBox::AcceptRole);
    m_apply->setEnabled(false);
    lay->addWidget(bb);

    connect(go, &QPushButton::clicked, this, &AlbumLookupDialog::search);
    connect(m_album, &QLineEdit::returnPressed, this, &AlbumLookupDialog::search);
    connect(m_artist, &QLineEdit::returnPressed, this, &AlbumLookupDialog::search);
    connect(m_results, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) {
        const int i = m_results->row(it);
        if (i < 0 || i >= m_found.size()) return;
        m_status->setText(QStringLiteral("Getting the song list…"));
        m_mb.fetchRelease(m_found[i].id);
    });
    connect(back, &QPushButton::clicked, this, [this] { m_stack->setCurrentIndex(0); m_apply->setEnabled(false); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &AlbumLookupDialog::apply);
    connect(&m_mb, &MusicBrainz::searchFinished, this, [this](const QVector<MbRelease> &r) {
        m_found = r;
        m_results->clear();
        for (const MbRelease &x : r)
            m_results->addItem(QStringLiteral("%1 — %2   (%3, %4 songs%5)").arg(x.title, x.artist,
                x.date.isEmpty() ? QStringLiteral("no date") : x.date).arg(x.trackCount)
                .arg(x.country.isEmpty() ? QString() : ", " + x.country));
        m_status->setText(r.isEmpty() ? QStringLiteral("No albums found. Try fewer words, or edit the info by hand.")
                                      : QStringLiteral("Double-click the matching album."));
    });
    connect(&m_mb, &MusicBrainz::releaseFinished, this, &AlbumLookupDialog::showRelease);
    connect(&m_mb, &MusicBrainz::failed, this, [this](const QString &m) {
        m_status->setText(QStringLiteral("Couldn't reach MusicBrainz: %1").arg(m));
    });
    connect(&m_mb, &MusicBrainz::coverFinished, this, [this](const QImage &img) {
        QString err;
        if (!m_library->pasteAlbumArt(m_tracks.value(0).displayAlbumArtist(), m_tracks.value(0).displayAlbum(), img, &err))
            QMessageBox::warning(this, QStringLiteral("Album art"), err);
        accept();
    });
    resize(680, 520);
}

void AlbumLookupDialog::search()
{
    m_status->setText(QStringLiteral("Searching MusicBrainz…"));
    m_mb.searchReleases(m_artist->text(), m_album->text());
}

void AlbumLookupDialog::showRelease(const MbReleaseDetail &d)
{
    m_release = d;
    m_matches->setRowCount(int(m_tracks.size()));
    // Match by position: disc/track number when the files have them, otherwise list order.
    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track &t = m_tracks[i];
        const MbTrack *m = nullptr;
        if (t.trackNo > 0)
            for (const MbTrack &x : d.tracks)
                if (x.position == t.trackNo && x.disc == qMax(1, t.discNo)) { m = &x; break; }
        if (!m && i < d.tracks.size()) m = &d.tracks[i];
        m_matches->setItem(i, 0, new QTableWidgetItem(t.displayTitle()));
        m_matches->setItem(i, 1, new QTableWidgetItem(m ? m->title : QString()));
        m_matches->setItem(i, 2, new QTableWidgetItem(m ? m->artist : QString()));
        m_matches->item(i, 0)->setFlags(Qt::ItemIsEnabled);
        if (m) m_matches->item(i, 1)->setData(Qt::UserRole, QPoint(m->disc, m->position));
    }
    m_status->setText(QStringLiteral("%1 — %2 (%3). Check the matches; you can edit titles before applying.")
                          .arg(d.title, d.artist).arg(d.year ? QString::number(d.year) : QStringLiteral("no year")));
    m_stack->setCurrentIndex(1);
    m_apply->setEnabled(true);
}

void AlbumLookupDialog::apply()
{
    QStringList errors;
    for (int i = 0; i < m_tracks.size(); ++i) {
        TagEdit e;
        e.album = m_release.title;
        e.albumArtist = m_release.artist;
        if (m_release.year) e.year = m_release.year;
        const QString title = m_matches->item(i, 1)->text();
        const QString artist = m_matches->item(i, 2)->text();
        if (!title.isEmpty()) e.title = title;
        if (!artist.isEmpty()) e.artist = artist;
        const QPoint pos = m_matches->item(i, 1)->data(Qt::UserRole).toPoint();
        if (pos.y() > 0) { e.trackNo = pos.y(); e.discNo = pos.x(); }
        m_library->editTags({m_tracks[i].id}, e, &errors);
    }
    // Refresh our copies so the cover lands with the new album name.
    for (Track &t : m_tracks) if (auto f = m_library->db()->track(t.id)) t = *f;
    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Some files weren't updated"), errors.join('\n').left(1500));
    if (m_cover->isChecked() && !m_release.id.isEmpty()) {
        m_status->setText(QStringLiteral("Downloading the cover…"));
        m_apply->setEnabled(false);
        m_mb.fetchCover(m_release.id);
        return;
    }
    accept();
}

} // namespace halo
