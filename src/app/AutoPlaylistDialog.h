#pragma once
#include "AutoPlaylist.h"
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QVBoxLayout;

namespace halo {

class AutoPlaylistDialog : public QDialog {
    Q_OBJECT
public:
    AutoPlaylistDialog(const QString &name, const AutoPlaylistSpec &spec, const TrackList &library,
                       QWidget *parent = nullptr);
    QString name() const;
    AutoPlaylistSpec spec() const;

private:
    struct Row {
        QWidget *widget; QComboBox *field; QComboBox *mode; QLineEdit *text;
        QSpinBox *min; QSpinBox *max; QComboBox *option; QLabel *toLabel;
    };
    void addRow(const AutoRule &rule);
    void updateRowWidgets(Row &r);
    void updatePreview();
    QLineEdit *m_name;
    QVBoxLayout *m_rowsLayout;
    QVector<Row> m_rows;
    QComboBox *m_arrange;
    QComboBox *m_limit;
    QSpinBox *m_limitCount;
    QLabel *m_preview;
    TrackList m_library;
};

} // namespace halo
