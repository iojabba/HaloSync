#include "DuplicateDialog.h"
#include "Library.h"
#include "TrackModel.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace halo {

DuplicateDialog::DuplicateDialog(Library *library, const QVector<DuplicateGroup> &groups, QWidget *parent)
    : QDialog(parent), m_lib(library), m_groups(groups)
{
    setWindowTitle(QStringLiteral("Review duplicates"));
    resize(980, 620);
    auto *l = new QVBoxLayout(this);
    auto *intro = new QLabel(QStringLiteral(
        "Ticked copies stay in your collection. Unticked copies are removed from it, and their play counts, "
        "ratings and playlist places move to the copy you keep. Halo Sync ticks the best copy for you: "
        "lossless first, then the highest bit rate, then the one with album art and the most plays."));
    intro->setWordWrap(true);
    l->addWidget(intro);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({QStringLiteral("SONG"), QStringLiteral("TYPE"), QStringLiteral("BITRATE"),
                             QStringLiteral("SIZE"), QStringLiteral("PLAYS"), QStringLiteral("LOCATION")});
    m_tree->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    for (int g = 0; g < m_groups.size(); ++g) {
        const DuplicateGroup &grp = m_groups[g];
        auto *top = new QTreeWidgetItem(m_tree, {grp.label + (grp.identical ? QStringLiteral("  ·  identical files")
                                                                            : QStringLiteral("  ·  same song, different files"))});
        QFont f = top->font(0);
        f.setBold(true);
        top->setFont(0, f);
        top->setFirstColumnSpanned(true);
        top->setFlags(Qt::ItemIsEnabled);
        for (int i = 0; i < grp.tracks.size(); ++i) {
            const Track &t = grp.tracks[i];
            auto *it = new QTreeWidgetItem(top, {t.displayAlbum(), t.format.toUpper(),
                                                 t.bitrateKbps ? QStringLiteral("%1 Kbps").arg(t.bitrateKbps) : QString(),
                                                 TrackModel::formatSize(t.sizeBytes), QString::number(t.playCount), t.path});
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            it->setCheckState(0, i == grp.best ? Qt::Checked : Qt::Unchecked);
            it->setData(0, Qt::UserRole, g);
            it->setData(1, Qt::UserRole, i);
            it->setToolTip(5, t.path);
        }
        top->setExpanded(true);
    }
    l->addWidget(m_tree, 1);

    m_trash = new QCheckBox(QStringLiteral("Also move the removed copies' files to the Recycle Bin / Trash"));
    l->addWidget(m_trash);
    m_summary = new QLabel;
    m_summary->setObjectName("dim");
    l->addWidget(m_summary);
    auto *buttons = new QDialogButtonBox;
    auto *apply = buttons->addButton(QStringLiteral("remove unticked copies"), QDialogButtonBox::AcceptRole);
    apply->setObjectName("accent");
    buttons->addButton(QDialogButtonBox::Close);
    l->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(apply, &QPushButton::clicked, this, &DuplicateDialog::apply);
    connect(m_tree, &QTreeWidget::itemChanged, this, &DuplicateDialog::updateSummary);
    updateSummary();
}

void DuplicateDialog::updateSummary()
{
    int remove = 0, skipped = 0;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *top = m_tree->topLevelItem(g);
        int kept = 0, unticked = 0;
        for (int i = 0; i < top->childCount(); ++i)
            (top->child(i)->checkState(0) == Qt::Checked ? kept : unticked)++;
        if (kept == 0) ++skipped;          // nothing kept: leave the group alone
        else remove += unticked;
    }
    QString text = QStringLiteral("%1 groups. %2 copies will be removed from the collection.").arg(m_groups.size()).arg(remove);
    if (skipped) text += QStringLiteral(" %1 groups with nothing ticked will be left as they are.").arg(skipped);
    m_summary->setText(text);
}

void DuplicateDialog::apply()
{
    QVector<qint64> removeIds;
    QVector<QPair<qint64, QVector<qint64>>> merges;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *top = m_tree->topLevelItem(g);
        qint64 keep = 0;
        QVector<qint64> drop;
        for (int i = 0; i < top->childCount(); ++i) {
            const Track &t = m_groups[g].tracks[top->child(i)->data(1, Qt::UserRole).toInt()];
            if (top->child(i)->checkState(0) == Qt::Checked) { if (!keep) keep = t.id; }
            else drop << t.id;
        }
        if (!keep || drop.isEmpty()) continue;
        merges.push_back({keep, drop});
        removeIds += drop;
    }
    if (removeIds.isEmpty()) { accept(); return; }
    const bool trash = m_trash->isChecked();
    const QString what = trash ? QStringLiteral("removed from the collection and their files moved to the Recycle Bin / Trash")
                               : QStringLiteral("removed from the collection (the files stay on disk)");
    if (QMessageBox::question(this, windowTitle(), QStringLiteral("%1 copies will be %2. Continue?").arg(removeIds.size()).arg(what))
        != QMessageBox::Yes)
        return;
    for (const auto &m : merges) m_lib->db()->mergeTrackStats(m.first, m.second);
    m_lib->removeFromCollection(removeIds, trash, true);
    accept();
}

} // namespace halo
