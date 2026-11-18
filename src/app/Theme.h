#pragma once
#include <QColor>
#include <QString>
#include <QVector>

class QApplication;

namespace halo {

// Original Metro-inspired styling: flat surfaces, big lowercase pivots,
// one accent colour. Uses system/open fonts only.
struct Theme {
    struct Preset { const char *name; QColor color; };
    static QVector<Preset> accentPresets();
    static void apply(QApplication &app);           // reads settings
    static void apply(QApplication &app, bool dark, const QColor &accent);
    static bool dark();
    static QColor accent();
    static QString styleSheet(bool dark, const QColor &accent);
};

} // namespace halo
