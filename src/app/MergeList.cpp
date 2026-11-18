#include "MergeList.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

namespace halo {

MergeList::MergeList(QWidget *parent) : QListWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
}

QString MergeList::currentKey() const
{
    return currentItem() ? currentItem()->data(KeyRole).toString() : QString();
}

void MergeList::dragEnterEvent(QDragEnterEvent *e)
{
    const bool tracks = m_trackDrop && e->mimeData()->hasFormat("application/x-halo-track-ids");
    const bool internal = m_merge && e->source() == this;
    if (tracks || internal) e->acceptProposedAction();
    else e->ignore();
}

void MergeList::dragMoveEvent(QDragMoveEvent *e)
{
    QListWidgetItem *target = itemAt(e->position().toPoint());
    if (!target) { e->ignore(); return; }
    if (e->source() == this && target == currentItem()) { e->ignore(); return; }
    e->acceptProposedAction();
}

void MergeList::dropEvent(QDropEvent *e)
{
    QListWidgetItem *target = itemAt(e->position().toPoint());
    if (!target) { e->ignore(); return; }
    if (m_trackDrop && e->mimeData()->hasFormat("application/x-halo-track-ids")) {
        QVector<qint64> ids;
        for (const QByteArray &part : e->mimeData()->data("application/x-halo-track-ids").split(','))
            if (!part.isEmpty()) ids << part.toLongLong();
        emit tracksDropped(target->data(KeyRole).toString(), ids);
        e->acceptProposedAction();
        return;
    }
    if (m_merge && e->source() == this && currentItem() && currentItem() != target) {
        emit mergeRequested(currentItem()->data(KeyRole).toString(), currentItem()->text(),
                            target->data(KeyRole).toString(), target->text());
    }
    // Never let Qt move the item itself; the list is rebuilt from the library.
    e->setDropAction(Qt::IgnoreAction);
    e->accept();
}

} // namespace halo
