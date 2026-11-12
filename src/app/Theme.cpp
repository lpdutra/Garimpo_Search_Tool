#include "Theme.h"

#include <QApplication>

namespace Theme {

namespace {

QColor mix(const QColor &a, const QColor &b, double amount)
{
    return QColor::fromRgbF(float(a.redF() * (1 - amount) + b.redF() * amount),
                            float(a.greenF() * (1 - amount) + b.greenF() * amount),
                            float(a.blueF() * (1 - amount) + b.blueF() * amount));
}

QString css(const QColor &color) { return color.name(QColor::HexArgb); }

} // namespace

bool isDark(const QPalette &palette) { return palette.color(QPalette::Window).lightness() < 128; }

QColor accent(const QPalette &palette)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    return palette.color(QPalette::Active, QPalette::Accent);
#else
    return palette.color(QPalette::Active, QPalette::Highlight);
#endif
}

QColor accentSoft(const QPalette &palette, double strength)
{
    return mix(palette.color(QPalette::Base), accent(palette), strength);
}

QColor matchBackground(const QPalette &palette)
{
    return isDark(palette) ? QColor(230, 180, 40) : QColor(255, 214, 64);
}

QColor currentMatchBackground() { return QColor(255, 140, 0); }

QColor dimText(const QPalette &palette, double alpha)
{
    QColor color = palette.color(QPalette::Text);
    color.setAlphaF(float(alpha));
    return color;
}

void apply(QApplication &app)
{
    const QPalette palette = app.palette();
    const QColor accentColor = accent(palette);
    const QColor text = palette.color(QPalette::Text);
    const QColor base = palette.color(QPalette::Base);
    const QColor window = palette.color(QPalette::Window);
    const QColor border = mix(window, text, isDark(palette) ? 0.22 : 0.18);
    const QColor hover = mix(window, text, 0.08);

    app.setStyleSheet(QStringLiteral(R"(
QTabBar::tab {
    padding: 5px 14px; margin: 4px 2px 4px 2px; border-radius: 6px;
    background: transparent; color: %1; min-width: 60px;
}
QTabBar::tab:hover { background: %2; }
QTabBar::tab:selected { background: %3; color: %1; font-weight: bold; }
QToolButton#modeButton {
    padding: 4px 14px; border: 1px solid %4; background: %5; color: %1;
}
QToolButton#modeButton:checked { background: %6; border-color: %6; color: white; font-weight: bold; }
QToolButton#modeButton[position="first"] { border-top-left-radius: 6px; border-bottom-left-radius: 6px; }
QToolButton#modeButton[position="last"] { border-top-right-radius: 6px; border-bottom-right-radius: 6px; }
QToolButton#filtersButton { border: none; padding: 3px 6px; color: %1; }
QToolButton#filtersButton:checked { color: %6; font-weight: bold; }
QSplitter::handle:horizontal { background: %4; width: 7px; }
QSplitter::handle:horizontal:hover, QSplitter::handle:horizontal:pressed { background: %6; }
QLabel#previewTitle { font-weight: bold; }
QLabel#previewPosition { color: %6; font-weight: bold; }
QWidget#previewHeader, QWidget#resultsHeader { background: %7; }
)")
                          .arg(css(text), css(hover), css(accentSoft(palette, 0.55)), css(border), css(base),
                               css(accentColor), css(accentSoft(palette, 0.08))));
}

} // namespace Theme
