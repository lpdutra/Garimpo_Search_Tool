#pragma once

#include <QColor>
#include <QPalette>

class QApplication;

/// Colors and the style sheet that give the app its accent-colored look.
namespace Theme {

void apply(QApplication &app);
bool isDark(const QPalette &palette);
QColor accent(const QPalette &palette);
/// Accent mixed into the background, for selected tabs and file headers.
QColor accentSoft(const QPalette &palette, double strength = 0.18);
QColor matchBackground(const QPalette &palette);
QColor currentMatchBackground();
QColor dimText(const QPalette &palette, double alpha = 0.55);

} // namespace Theme
