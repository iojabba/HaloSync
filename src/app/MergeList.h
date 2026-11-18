#pragma once
#include <QListWidget>

namespace halo {

// List of artists / albums / genres / playlists.
//  - Dragging one entry onto another asks to merge them (SPEC §2.4 "Merge info").
//  - Dropping songs onto an entry emits tracksDropped (used for playlists).
class MergeList : public QListWidget {
    Q_OBJECT
public:
    static constexpr int KeyRole = Qt::UserRole + 10;
    explicit MergeList(QWidget *parent = nullptr);
    void setMergeEnabled(bool on) { m_merge = on; }
    void setTrackDropEnabled(bool on) { m_trackDrop = on; }
    QString currentKey() const;

signals:
    void mergeRequested(const QString &fromKey, const QString &fromLabel, const QString &toKey, const QString &toLabel);
    void tracksDropped(const QString &targetKey, const QVector<qint64> &trackIds);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    bool m_merge = true;
    bool m_trackDrop = false;
};

} // namespace halo
