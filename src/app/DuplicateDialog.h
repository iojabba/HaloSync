#pragma once
#include "Reports.h"
#include <QDialog>

class QCheckBox;
class QLabel;
class QTreeWidget;

namespace halo {

class Library;

// Duplicate review (SPEC §5, 2b): each group of copies with the best one
// ticked. Unticked copies leave the collection; their plays, ratings and
// playlist places move to the copy that stays.
class DuplicateDialog : public QDialog {
    Q_OBJECT
public:
    DuplicateDialog(Library *library, const QVector<DuplicateGroup> &groups, QWidget *parent = nullptr);
private:
    void apply();
    void updateSummary();
    Library *m_lib;
    QVector<DuplicateGroup> m_groups;
    QTreeWidget *m_tree;
    QCheckBox *m_trash;
    QLabel *m_summary;
};

} // namespace halo
