#include "PreviewView.h"

#include <QFontDatabase>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>

using namespace garimpo;

namespace {

/// Highlighting every match in a huge file would slow the editor down.
constexpr int kMaxHighlights = 20000;

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(PreviewView *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->lineNumberAreaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->paintLineNumbers(event); }

private:
    PreviewView *m_editor;
};

bool isDark(const QPalette &palette) { return palette.color(QPalette::Base).lightness() < 128; }

} // namespace

PreviewView::PreviewView(QWidget *parent) : QPlainTextEdit(parent), m_lineNumbers(new LineNumberArea(this))
{
    setReadOnly(true);
    setLineWrapMode(NoWrap);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, &PreviewView::updateLineNumberArea);
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void PreviewView::setFontSize(int size)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(size);
    setFont(font);
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

int PreviewView::lineNumberAreaWidth() const
{
    const int digits = qMax(3, int(QString::number(qMax(1, blockCount())).size()));
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void PreviewView::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    m_lineNumbers->setGeometry(QRect(area.left(), area.top(), lineNumberAreaWidth(), area.height()));
}

void PreviewView::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy) m_lineNumbers->scroll(0, dy);
    else m_lineNumbers->update(0, rect.y(), m_lineNumbers->width(), rect.height());
}

void PreviewView::paintLineNumbers(QPaintEvent *event)
{
    QPainter painter(m_lineNumbers);
    QColor gutter = palette().color(QPalette::Base);
    gutter = isDark(palette()) ? gutter.lighter(115) : gutter.darker(104);
    painter.fillRect(event->rect(), gutter);
    QColor color = palette().color(QPalette::Text);
    color.setAlphaF(0.45f);
    painter.setPen(color);
    painter.setFont(font());

    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int width = m_lineNumbers->width() - 6;
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top())
            painter.drawText(0, top, width, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void PreviewView::showMessage(const QString &message)
{
    m_path.clear();
    m_allMatches.clear();
    m_matches.clear();
    setExtraSelections({});
    setPlainText(message);
}

QTextCursor PreviewView::rangeCursor(int lineNumber, const LineMatch &match, const Span &span) const
{
    const QTextBlock block = document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid()) return {};
    // The stored line can be a clipped window of a very long line; find where it starts.
    int shift = 0;
    const QString blockText = block.text();
    if (match.text.size() < blockText.size()) {
        const qsizetype found = blockText.indexOf(match.text);
        if (found > 0) shift = int(found);
    }
    const int start = shift + span.start;
    if (start + span.length > blockText.size()) return {};
    QTextCursor cursor(block);
    cursor.setPosition(block.position() + start);
    cursor.setPosition(block.position() + start + span.length, QTextCursor::KeepAnchor);
    return cursor;
}

void PreviewView::showText(const QString &path, const QString &text, const FileResult &file)
{
    m_path = path;
    m_matches = file.matches;
    setPlainText(text);

    QTextCharFormat format;
    format.setBackground(isDark(palette()) ? QColor(214, 170, 40, 110) : QColor(255, 214, 64, 150));
    m_allMatches.clear();
    for (const LineMatch &match : m_matches) {
        for (const Span &span : match.spans) {
            if (m_allMatches.size() >= kMaxHighlights) break;
            const QTextCursor cursor = rangeCursor(match.lineNumber, match, span);
            if (!cursor.isNull()) m_allMatches.append({cursor, format});
        }
    }
    setExtraSelections(m_allMatches);
}

void PreviewView::selectMatch(const LineMatch *match)
{
    QList<QTextEdit::ExtraSelection> selections = m_allMatches;
    if (!match) {
        setExtraSelections(selections);
        return;
    }
    const QTextBlock block = document()->findBlockByNumber(match->lineNumber - 1);
    if (!block.isValid()) return;

    QTextEdit::ExtraSelection line;
    line.format.setBackground(palette().color(QPalette::Highlight).lighter(isDark(palette()) ? 60 : 170));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = QTextCursor(block);
    selections.prepend(line);

    QTextCursor first;
    QTextCharFormat current;
    current.setBackground(QColor(255, 140, 0, 220));
    current.setForeground(Qt::black);
    for (const Span &span : match->spans) {
        const QTextCursor cursor = rangeCursor(match->lineNumber, *match, span);
        if (cursor.isNull()) continue;
        if (first.isNull()) first = cursor;
        selections.append({cursor, current});
    }
    setExtraSelections(selections);

    QTextCursor caret = first.isNull() ? QTextCursor(block) : first;
    caret.setPosition(caret.selectionStart());
    setTextCursor(caret);
    centerCursor();
    // Scroll sideways only as much as needed to show the end of the match.
    QTextCursor endCursor = first.isNull() ? caret : first;
    endCursor.setPosition(endCursor.selectionEnd());
    QScrollBar *bar = horizontalScrollBar();
    const int matchEnd = cursorRect(endCursor).right() + bar->value();
    const int visible = viewport()->width() - 30;
    bar->setValue(matchEnd < visible ? 0 : matchEnd - visible);
}
