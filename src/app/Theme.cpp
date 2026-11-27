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

QColor syntaxColor(const QPalette &palette, garimpo::SyntaxToken token)
{
    using garimpo::SyntaxToken;
    const bool dark = isDark(palette);
    switch (token) {
    case SyntaxToken::Keyword: return dark ? QColor(0x56, 0x9C, 0xD6) : QColor(0x00, 0x33, 0xB3);
    case SyntaxToken::Type: return dark ? QColor(0x4E, 0xC9, 0xB0) : QColor(0x26, 0x7F, 0x99);
    case SyntaxToken::Constant: return dark ? QColor(0xC5, 0x86, 0xC0) : QColor(0xAF, 0x00, 0xDB);
    case SyntaxToken::Number: return dark ? QColor(0xB5, 0xCE, 0xA8) : QColor(0x09, 0x86, 0x58);
    case SyntaxToken::String:
    case SyntaxToken::Code: return dark ? QColor(0xCE, 0x91, 0x78) : QColor(0xA3, 0x15, 0x15);
    case SyntaxToken::Comment: return dark ? QColor(0x6A, 0x99, 0x55) : QColor(0x00, 0x80, 0x00);
    case SyntaxToken::Preprocessor: return dark ? QColor(0xD7, 0xBA, 0x7D) : QColor(0x9B, 0x4F, 0x00);
    case SyntaxToken::Tag: return dark ? QColor(0x56, 0x9C, 0xD6) : QColor(0x80, 0x00, 0x00);
    case SyntaxToken::Attribute:
    case SyntaxToken::Variable: return dark ? QColor(0x9C, 0xDC, 0xFE) : QColor(0x04, 0x51, 0xA5);
    case SyntaxToken::Heading:
    case SyntaxToken::Emphasis: return dark ? QColor(0x56, 0x9C, 0xD6) : QColor(0x00, 0x33, 0xB3);
    }
    return palette.color(QPalette::Text);
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
QToolButton#highlightButton { border: 1px solid %6; border-radius: 5px; padding: 1px 8px; color: %6; }
QToolButton#highlightButton:hover { background: %6; color: white; }
QWidget#syntaxNotice { background: %8; }
)")
                          .arg(css(text), css(hover), css(accentSoft(palette, 0.55)), css(border), css(base),
                               css(accentColor), css(accentSoft(palette, 0.08)),
                               css(mix(base, QColor(255, 170, 0), isDark(palette) ? 0.22 : 0.16))));
}

} // namespace Theme
