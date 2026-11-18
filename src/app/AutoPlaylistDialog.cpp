#include "AutoPlaylistDialog.h"
#include "TrackModel.h"
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QVBoxLayout>

namespace halo {

AutoPlaylistDialog::AutoPlaylistDialog(const QString &name, const AutoPlaylistSpec &spec, const TrackList &library,
                                       QWidget *parent)
    : QDialog(parent), m_library(library)
{
    setWindowTitle(QStringLiteral("Autoplaylist"));
    auto *lay = new QVBoxLayout(this);
    auto *h = new QLabel(name.isEmpty() ? QStringLiteral("new autoplaylist") : QStringLiteral("edit autoplaylist"));
    h->setObjectName("h1");
    lay->addWidget(h);
    auto *blurb = new QLabel(QStringLiteral("Autoplaylists update themselves from the rules you set. "
                                            "Separate several values with a semicolon, like Pop;Rock."));
    blurb->setObjectName("dim");
    blurb->setWordWrap(true);
    lay->addWidget(blurb);

    auto *form = new QFormLayout;
    m_name = new QLineEdit(name);
    m_name->setPlaceholderText(QStringLiteral("Playlist name"));
    form->addRow(QStringLiteral("Name"), m_name);
    lay->addLayout(form);

    auto *rulesHeader = new QLabel(QStringLiteral("MUSIC FILTERS"));
    rulesHeader->setObjectName("h2");
    lay->addWidget(rulesHeader);
    m_rowsLayout = new QVBoxLayout;
    lay->addLayout(m_rowsLayout);
    auto *add = new QPushButton(QStringLiteral("add filter"));
    add->setObjectName("flat");
    connect(add, &QPushButton::clicked, this, [this] { addRow(AutoRule{}); updatePreview(); });
    lay->addWidget(add, 0, Qt::AlignLeft);

    auto *form2 = new QFormLayout;
    m_arrange = new QComboBox;
    m_arrange->addItems(AutoPlaylistSpec::arrangeNames());
    m_arrange->setCurrentIndex(spec.arrange);
    form2->addRow(QStringLiteral("Arrange songs"), m_arrange);
    auto *limitRow = new QHBoxLayout;
    m_limit = new QComboBox;
    m_limit->addItems({QStringLiteral("No limit"), QStringLiteral("Songs"), QStringLiteral("Minutes")});
    m_limit->setCurrentIndex(spec.limit);
    m_limitCount = new QSpinBox;
    m_limitCount->setRange(1, 100000);
    m_limitCount->setValue(spec.limitCount > 0 ? spec.limitCount : 50);
    limitRow->addWidget(m_limit);
    limitRow->addWidget(m_limitCount);
    limitRow->addStretch();
    form2->addRow(QStringLiteral("Limit to"), limitRow);
    lay->addLayout(form2);

    m_preview = new QLabel;
    m_preview->setObjectName("dim");
    lay->addWidget(m_preview);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, [this] { if (!m_name->text().trimmed().isEmpty()) accept(); else m_name->setFocus(); });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    for (const AutoRule &r : spec.rules) addRow(r);
    if (spec.rules.isEmpty()) addRow(AutoRule{});
    connect(m_arrange, &QComboBox::currentIndexChanged, this, &AutoPlaylistDialog::updatePreview);
    connect(m_limit, &QComboBox::currentIndexChanged, this, [this] { m_limitCount->setEnabled(m_limit->currentIndex() != 0); updatePreview(); });
    connect(m_limitCount, &QSpinBox::valueChanged, this, &AutoPlaylistDialog::updatePreview);
    m_limitCount->setEnabled(m_limit->currentIndex() != 0);
    updatePreview();
    resize(640, 520);
}

void AutoPlaylistDialog::addRow(const AutoRule &rule)
{
    Row r;
    r.widget = new QWidget;
    auto *h = new QHBoxLayout(r.widget);
    h->setContentsMargins(0, 0, 0, 0);
    r.field = new QComboBox;
    r.field->addItems(AutoRule::fieldNames());
    r.field->setCurrentIndex(rule.field);
    r.mode = new QComboBox;
    r.mode->addItems({QStringLiteral("Include"), QStringLiteral("Exclude")});
    r.mode->setCurrentIndex(rule.mode);
    r.text = new QLineEdit(rule.values.join(';'));
    r.text->setPlaceholderText(QStringLiteral("value;value"));
    r.min = new QSpinBox;
    r.max = new QSpinBox;
    r.min->setRange(0, 9999);
    r.max->setRange(0, 9999);
    r.toLabel = new QLabel(QStringLiteral("to"));
    r.option = new QComboBox;
    auto *remove = new QPushButton(QStringLiteral("✕"));
    remove->setObjectName("flat");
    h->addWidget(r.field);
    h->addWidget(r.mode);
    h->addWidget(r.text, 1);
    h->addWidget(r.option);
    h->addWidget(r.min);
    h->addWidget(r.toLabel);
    h->addWidget(r.max);
    h->addWidget(remove);
    m_rowsLayout->addWidget(r.widget);
    m_rows.push_back(r);

    const int idx = int(m_rows.size()) - 1;
    updateRowWidgets(m_rows[idx]);
    // Restore the numeric/option values after the widgets are configured.
    Row &rr = m_rows[idx];
    if (rule.field == AutoRule::Rating) rr.option->setCurrentIndex(rule.rating);
    else if (rule.field == AutoRule::Plays) { rr.option->setCurrentIndex(rule.playsOp); rr.min->setValue(rule.playsN); }
    else if (rule.field == AutoRule::Year) { rr.min->setValue(rule.min); rr.max->setValue(rule.max); }
    else if (rule.field == AutoRule::AddedInLastDays) rr.min->setValue(rule.min > 0 ? rule.min : 30);

    QWidget *w = r.widget;
    connect(r.field, &QComboBox::currentIndexChanged, this, [this, w] {
        for (Row &x : m_rows) if (x.widget == w) updateRowWidgets(x);
        updatePreview();
    });
    for (QComboBox *c : {r.mode, r.option}) connect(c, &QComboBox::currentIndexChanged, this, &AutoPlaylistDialog::updatePreview);
    connect(r.text, &QLineEdit::textChanged, this, &AutoPlaylistDialog::updatePreview);
    connect(r.min, &QSpinBox::valueChanged, this, &AutoPlaylistDialog::updatePreview);
    connect(r.max, &QSpinBox::valueChanged, this, &AutoPlaylistDialog::updatePreview);
    connect(remove, &QPushButton::clicked, this, [this, w] {
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].widget == w) { m_rows.removeAt(i); break; }
        w->deleteLater();
        updatePreview();
    });
}

void AutoPlaylistDialog::updateRowWidgets(Row &r)
{
    const auto f = AutoRule::Field(r.field->currentIndex());
    const bool text = f <= AutoRule::Album;
    r.text->setVisible(text);
    r.mode->setVisible(text || f == AutoRule::Rating);
    r.option->setVisible(f == AutoRule::Rating || f == AutoRule::Plays);
    r.min->setVisible(f == AutoRule::Year || f == AutoRule::AddedInLastDays || f == AutoRule::Plays);
    r.max->setVisible(f == AutoRule::Year);
    r.toLabel->setVisible(f == AutoRule::Year);
    r.option->blockSignals(true);
    r.option->clear();
    if (f == AutoRule::Rating)
        r.option->addItems({QStringLiteral("All"), QStringLiteral("Like"), QStringLiteral("Don't like"),
                            QStringLiteral("Unrated"), QStringLiteral("Like or unrated")});
    else if (f == AutoRule::Plays)
        r.option->addItems({QStringLiteral("All"), QStringLiteral("Never played"), QStringLiteral("Less than"),
                            QStringLiteral("More than")});
    r.option->blockSignals(false);
    r.min->setSuffix(f == AutoRule::AddedInLastDays ? QStringLiteral(" days") : f == AutoRule::Plays ? QStringLiteral(" plays") : QString());
    r.min->setSpecialValueText(f == AutoRule::Year ? QStringLiteral("any") : QString());
    r.max->setSpecialValueText(QStringLiteral("any"));
    if (f == AutoRule::AddedInLastDays && r.min->value() == 0) r.min->setValue(30);

    if (text) {
        // Autocomplete from the library's values for this field.
        QSet<QString> values;
        for (const Track &t : m_library) {
            switch (f) {
            case AutoRule::AlbumArtist: values.insert(t.displayAlbumArtist()); break;
            case AutoRule::Artist:      values.insert(t.displayArtist()); break;
            case AutoRule::Composer:    if (!t.composer.isEmpty()) values.insert(t.composer); break;
            case AutoRule::Genre:       values.insert(t.displayGenre()); break;
            case AutoRule::Album:       values.insert(t.displayAlbum()); break;
            default: break;
            }
        }
        QStringList list(values.begin(), values.end());
        list.sort(Qt::CaseInsensitive);
        auto *c = new QCompleter(list, r.text);
        c->setCaseSensitivity(Qt::CaseInsensitive);
        c->setFilterMode(Qt::MatchContains);
        r.text->setCompleter(c);
    }
}

QString AutoPlaylistDialog::name() const { return m_name->text().trimmed(); }

AutoPlaylistSpec AutoPlaylistDialog::spec() const
{
    AutoPlaylistSpec s;
    for (const Row &r : m_rows) {
        AutoRule rule;
        rule.field = AutoRule::Field(r.field->currentIndex());
        rule.mode = AutoRule::Mode(r.mode->currentIndex());
        switch (rule.field) {
        case AutoRule::Rating: rule.rating = r.option->currentIndex(); break;
        case AutoRule::Plays: rule.playsOp = r.option->currentIndex(); rule.playsN = r.min->value(); break;
        case AutoRule::Year: rule.min = r.min->value(); rule.max = r.max->value(); break;
        case AutoRule::AddedInLastDays: rule.min = r.min->value(); break;
        default: rule.values = r.text->text().split(';', Qt::SkipEmptyParts); break;
        }
        s.rules.push_back(rule);
    }
    s.arrange = m_arrange->currentIndex();
    s.limit = m_limit->currentIndex();
    s.limitCount = m_limitCount->value();
    return s;
}

void AutoPlaylistDialog::updatePreview()
{
    const TrackList result = spec().evaluate(m_library, QDateTime::currentSecsSinceEpoch());
    qint64 ms = 0;
    for (const Track &t : result) ms += t.durationMs;
    m_preview->setText(result.isEmpty() ? QStringLiteral("None of the songs in your collection fit these rules.")
                                        : QStringLiteral("%1 songs, %2 minutes").arg(result.size()).arg(ms / 60000));
}

} // namespace halo
