#include "Theme.h"
#include "Paths.h"
#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace halo {

QVector<Theme::Preset> Theme::accentPresets()
{
    return {{"magenta", QColor(0xE3, 0x1E, 0x7B)}, {"orange", QColor(0xF0, 0x7A, 0x1A)},
            {"lime", QColor(0x8C, 0xBF, 0x26)},    {"cobalt", QColor(0x1E, 0x6F, 0xE0)},
            {"violet", QColor(0x8A, 0x3F, 0xD1)},  {"teal", QColor(0x00, 0xA3, 0x9A)}};
}

bool Theme::dark() { return Paths::settings()->value("ui/dark", true).toBool(); }

QColor Theme::accent()
{
    const QColor c(Paths::settings()->value("ui/accent", "#E31E7B").toString());
    return c.isValid() ? c : QColor(0xE3, 0x1E, 0x7B);
}

void Theme::apply(QApplication &app) { apply(app, dark(), accent()); }

void Theme::apply(QApplication &app, bool dark, const QColor &accent)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont f = app.font();
    f.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Inter"), QStringLiteral("Open Sans"),
                   QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")});
    f.setPointSizeF(10);
    app.setFont(f);

    QPalette p;
    const QColor bg = dark ? QColor(0x14, 0x14, 0x14) : QColor(0xFA, 0xFA, 0xFA);
    const QColor base = dark ? QColor(0x1C, 0x1C, 0x1C) : QColor(0xFF, 0xFF, 0xFF);
    const QColor text = dark ? QColor(0xF2, 0xF2, 0xF2) : QColor(0x1A, 0x1A, 0x1A);
    const QColor mid = dark ? QColor(0x8A, 0x8A, 0x8A) : QColor(0x70, 0x70, 0x70);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, dark ? QColor(0x22, 0x22, 0x22) : QColor(0xF3, 0xF3, 0xF3));
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Button, base);
    p.setColor(QPalette::PlaceholderText, mid);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Disabled, QPalette::Text, mid);
    p.setColor(QPalette::Disabled, QPalette::WindowText, mid);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, mid);
    app.setPalette(p);
    app.setStyleSheet(styleSheet(dark, accent));
}

QString Theme::styleSheet(bool dark, const QColor &accent)
{
    const QString a = accent.name();
    const QString dim = dark ? "#7a7a7a" : "#8a8a8a";
    const QString text = dark ? "#f2f2f2" : "#1a1a1a";
    const QString line = dark ? "#2c2c2c" : "#e2e2e2";
    const QString hover = dark ? "#262626" : "#ededed";
    return QStringLiteral(R"(
QWidget { outline: 0; }
QPushButton#pivot { border: none; background: transparent; color: %2; font-size: 26px; font-weight: 300;
                    padding: 0 14px 4px 0; text-align: left; }
QPushButton#pivot:hover { color: %3; }
QPushButton#pivot:checked { color: %3; }
QPushButton#subpivot { border: none; background: transparent; color: %2; font-size: 15px; font-weight: 600;
                       padding: 2px 14px 2px 0; }
QPushButton#subpivot:hover, QPushButton#subpivot:checked { color: %1; }
QLabel#brand { color: %1; font-size: 13px; font-weight: 700; letter-spacing: 2px; }
QLabel#h1 { font-size: 30px; font-weight: 300; }
QLabel#h2 { font-size: 13px; font-weight: 700; color: %2; letter-spacing: 1px; }
QLabel#nowTitle { font-size: 15px; font-weight: 600; }
QLabel#nowSub, QLabel#dim { color: %2; }
QLabel#notice { background: %1; color: white; padding: 10px; }
QPushButton { border: 1px solid %4; padding: 5px 14px; background: transparent; }
QPushButton:hover { background: %5; }
QPushButton:pressed { background: %1; color: white; border-color: %1; }
QPushButton:checked { color: %1; border-color: %1; }
QPushButton:disabled { color: %2; }
QPushButton#accent { background: %1; color: white; border: 1px solid %1; font-weight: 600; }
QPushButton#transport { border: 2px solid %2; border-radius: 17px; min-width: 30px; min-height: 30px;
                        max-width: 30px; max-height: 30px; padding: 0; font-size: 13px; }
QPushButton#transport:hover { border-color: %3; }
QPushButton#transport:checked { border-color: %1; color: %1; }
QPushButton#flat { border: none; padding: 3px 8px; }
QFrame#transportBar { border-top: 1px solid %4; }
QLineEdit, QSpinBox, QComboBox { border: 1px solid %4; padding: 4px 6px; background: palette(base); }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: %1; }
QTableView, QListView, QListWidget, QTreeView { border: none; background: palette(window); selection-background-color: %1; }
QTableView::item { padding: 2px 6px; border: none; }
QListWidget::item { padding: 5px 4px; }
QListWidget::item:hover, QTableView::item:hover { background: %5; }
QListWidget::item:selected, QTableView::item:selected { background: %1; color: white; }
QHeaderView::section { background: palette(window); border: none; border-bottom: 1px solid %4;
                       padding: 4px 6px; color: %2; font-weight: 600; }
QSlider::groove:horizontal { height: 3px; background: %4; }
QSlider::sub-page:horizontal { background: %1; }
QSlider::handle:horizontal { width: 10px; margin: -4px 0; background: %3; }
QProgressBar { border: none; background: %4; height: 6px; text-align: center; max-height: 6px; }
QProgressBar::chunk { background: %1; }
QScrollBar:vertical { width: 8px; background: transparent; }
QScrollBar::handle:vertical { background: %4; min-height: 30px; }
QScrollBar:horizontal { height: 8px; background: transparent; }
QScrollBar::handle:horizontal { background: %4; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QMenu { background: palette(base); border: 1px solid %4; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 14px; }
QMenu::item:selected { background: %1; color: white; }
QSplitter::handle { background: %4; }
QSplitter::handle:horizontal { width: 1px; }
QGroupBox { border: none; margin-top: 18px; font-weight: 700; }
QGroupBox::title { subcontrol-origin: margin; left: 0; color: %2; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %1; border: 1px solid %1; }
QToolTip { border: 1px solid %4; padding: 4px; }
)").arg(a, dim, text, line, hover);
}

} // namespace halo
