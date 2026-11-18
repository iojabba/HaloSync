#include "TagEditorDialog.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QVBoxLayout>

namespace halo {

namespace { enum F { FTitle, FArtist, FAlbumArtist, FAlbum, FGenre, FComposer, FYear, FTrack, FDisc }; }

TagEditorDialog::TagEditorDialog(const TrackList &tracks, QWidget *parent) : QDialog(parent), m_tracks(tracks)
{
    setWindowTitle(tracks.size() == 1 ? QStringLiteral("Edit info") : QStringLiteral("Edit info for %1 songs").arg(tracks.size()));
    auto *lay = new QVBoxLayout(this);
    auto *h = new QLabel(tracks.size() == 1 ? QStringLiteral("edit info") : QStringLiteral("edit %1 songs").arg(tracks.size()));
    h->setObjectName("h1");
    lay->addWidget(h);
    auto *form = new QFormLayout;
    lay->addLayout(form);

    auto collect = [&](auto getter) {
        QStringList v;
        for (const Track &t : tracks) v << getter(t);
        return v;
    };
    auto num = [](int n) { return n > 0 ? QString::number(n) : QString(); };
    const bool single = tracks.size() == 1;
    if (single) addField(form, "Title", collect([](const Track &t) { return t.title; }), false);
    else { m_fields << nullptr; m_numeric << false; }
    addField(form, "Contributing artist", collect([](const Track &t) { return t.artist; }), false);
    addField(form, "Album artist", collect([](const Track &t) { return t.albumArtist; }), false);
    addField(form, "Album", collect([](const Track &t) { return t.album; }), false);
    addField(form, "Genre", collect([](const Track &t) { return t.genre; }), false);
    addField(form, "Composer", collect([](const Track &t) { return t.composer; }), false);
    addField(form, "Release year", collect([&](const Track &t) { return num(t.year); }), true);
    if (single) addField(form, "Track number", collect([&](const Track &t) { return num(t.trackNo); }), true);
    else { m_fields << nullptr; m_numeric << true; }
    addField(form, "Disc", collect([&](const Track &t) { return num(t.discNo); }), true);

    auto *note = new QLabel(QStringLiteral("Changes are written into the music files."));
    note->setObjectName("dim");
    lay->addWidget(note);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    resize(460, sizeHint().height());
}

QLineEdit *TagEditorDialog::addField(QFormLayout *form, const QString &label, const QStringList &values, bool numeric)
{
    auto *e = new QLineEdit;
    const QSet<QString> distinct(values.begin(), values.end());
    if (distinct.size() == 1) e->setText(values.value(0));
    else e->setPlaceholderText(QStringLiteral("(multiple values)"));
    e->setModified(false);
    if (numeric) e->setValidator(new QIntValidator(0, 9999, e));
    form->addRow(label, e);
    m_fields << e;
    m_numeric << numeric;
    return e;
}

TagEdit TagEditorDialog::edit() const
{
    TagEdit out;
    auto text = [&](int i) -> std::optional<QString> {
        QLineEdit *e = m_fields.value(i);
        if (!e || !e->isModified()) return std::nullopt;
        return e->text();
    };
    auto number = [&](int i) -> std::optional<int> {
        auto t = text(i);
        if (!t) return std::nullopt;
        return t->trimmed().toInt();
    };
    out.title = text(FTitle);
    out.artist = text(FArtist);
    out.albumArtist = text(FAlbumArtist);
    out.album = text(FAlbum);
    out.genre = text(FGenre);
    out.composer = text(FComposer);
    out.year = number(FYear);
    out.trackNo = number(FTrack);
    out.discNo = number(FDisc);
    return out;
}

} // namespace halo
