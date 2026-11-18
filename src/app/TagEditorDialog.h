#pragma once
#include "TagIO.h"
#include <QDialog>

class QLineEdit;
class QSpinBox;

class QFormLayout;

namespace halo {

// Edit info for one or many songs (SPEC §2.4). With several songs selected,
// fields that differ show "(multiple values)" and are only written if changed.
class TagEditorDialog : public QDialog {
    Q_OBJECT
public:
    TagEditorDialog(const TrackList &tracks, QWidget *parent = nullptr);
    TagEdit edit() const;

private:
    struct Field { QLineEdit *edit; bool common; };
    QLineEdit *addField(QFormLayout *form, const QString &label, const QStringList &values, bool numeric);
    QVector<QLineEdit *> m_fields;
    QVector<bool> m_numeric;
    TrackList m_tracks;
};

} // namespace halo
