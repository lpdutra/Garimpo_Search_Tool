#include "PreviewView.h"

#include "Theme.h"

#include <QFontDatabase>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>

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
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &PreviewView::highlightVisibleLines);
}

void PreviewView::setFontSize(int size)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(size);
    setFont(font);
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    highlightVisibleLines();
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
    highlightVisibleLines();
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

void PreviewView::clearSyntax()
{
    m_language = nullptr;
    m_lineStates.clear();
    m_colored.clear();
}

void PreviewView::setSyntax(const SyntaxLanguage *language, std::vector<int> lineStates)
{
    clearSyntax();
    if (!language) return;
    m_language = language;
    // Lines that do not line up with the document start plain; the visible ones still get colors.
    if (lineStates.size() == size_t(blockCount())) m_lineStates = std::move(lineStates);
    m_colored.assign(size_t(blockCount()), false);
    for (int i = 0; i < kSyntaxTokenCount; ++i) {
        const auto token = SyntaxToken(i);
        QTextCharFormat format;
        format.setForeground(Theme::syntaxColor(palette(), token));
        if (token == SyntaxToken::Comment) format.setFontItalic(true);
        if (token == SyntaxToken::Heading || token == SyntaxToken::Emphasis) format.setFontWeight(QFont::Bold);
        m_syntaxFormats[i] = format;
    }
    highlightVisibleLines();
}

void PreviewView::highlightVisibleLines()
{
    if (!m_language) return;
    // Color a screen ahead as well, so scrolling does not show plain text first.
    const int limit = viewport()->height() * 2;
    const QPointF offset = contentOffset();
    std::vector<SyntaxSpan> spans;
    for (QTextBlock block = firstVisibleBlock(); block.isValid(); block = block.next()) {
        if (blockBoundingGeometry(block).translated(offset).top() > limit) break;
        const size_t number = size_t(block.blockNumber());
        if (number >= m_colored.size() || m_colored[number]) continue;
        m_colored[number] = true;
        spans.clear();
        const int state = number < m_lineStates.size() ? m_lineStates[number] : 0;
        Syntax::highlightLine(*m_language, block.text(), state, &spans);
        if (spans.empty()) continue;
        QList<QTextLayout::FormatRange> ranges;
        ranges.reserve(qsizetype(spans.size()));
        for (const SyntaxSpan &span : spans) ranges.append({span.start, span.length, m_syntaxFormats[int(span.token)]});
        block.layout()->setFormats(ranges);
        document()->markContentsDirty(block.position(), block.length());
    }
}

void PreviewView::showMessage(const QString &message)
{
    clearSyntax();
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
    clearSyntax();
    m_path = path;
    m_matches = file.matches;
    setPlainText(text);

    // Dark text on the match color stays readable over any syntax color, as in the result list.
    QTextCharFormat format;
    format.setBackground(Theme::matchBackground(palette()));
    format.setForeground(Qt::black);
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
    line.format.setBackground(Theme::accentSoft(palette(), isDark(palette()) ? 0.30 : 0.22));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = QTextCursor(block);
    selections.prepend(line);

    QTextCursor first;
    QTextCharFormat current;
    current.setBackground(Theme::currentMatchBackground());
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
