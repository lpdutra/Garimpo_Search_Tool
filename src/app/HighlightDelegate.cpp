#include "HighlightDelegate.h"

#include "ResultsModel.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>

using namespace garimpo;

namespace {

constexpr int kLineNumberWidth = 56;
constexpr int kPadding = 8;

QColor highlightColor(const QPalette &palette)
{
    const bool dark = palette.color(QPalette::Base).lightness() < 128;
    return dark ? QColor(214, 170, 40, 170) : QColor(255, 214, 64, 200);
}

/// Draws `text` from `x`, painting the spans behind it, and returns where it stopped.
void drawSpans(QPainter *painter, const QFontMetrics &metrics, const QRect &rect, int x, const QString &text,
               const Spans &spans, const QColor &mark, const QColor &textColor, const QColor &markText)
{
    const int baseline = rect.top() + (rect.height() + metrics.ascent() - metrics.descent()) / 2;
    const int right = rect.right() - kPadding;
    qsizetype cursor = 0;
    auto drawPiece = [&](qsizetype from, qsizetype length, bool marked) {
        if (length <= 0 || x >= right) return;
        QString piece = text.mid(from, length);
        piece.replace(QLatin1Char('\t'), QLatin1Char(' '));
        const int width = metrics.horizontalAdvance(piece);
        if (marked) {
            const QRect box(x - 1, rect.top() + 2, qMin(width + 2, right - x + 1), rect.height() - 4);
            painter->fillRect(box, mark);
        }
        painter->setPen(marked ? markText : textColor);
        if (x + width > right) {
            painter->drawText(x, baseline, metrics.elidedText(piece, Qt::ElideRight, right - x));
            x = right;
        } else {
            painter->drawText(x, baseline, piece);
            x += width;
        }
    };
    for (const Span &span : spans) {
        if (span.start < cursor || span.start + span.length > text.size()) continue;
        drawPiece(cursor, span.start - cursor, false);
        drawPiece(span.start, span.length, true);
        cursor = span.start + span.length;
    }
    drawPiece(cursor, text.size() - cursor, false);
}

} // namespace

HighlightDelegate::HighlightDelegate(ResultsModel *model, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_model(model)
    , m_mono(QFontDatabase::systemFont(QFontDatabase::FixedFont))
    , m_fileIcon(QApplication::style()->standardIcon(QStyle::SP_FileIcon))
    , m_documentIcon(QIcon::fromTheme(QStringLiteral("x-office-document"), m_fileIcon))
{
}

QSize HighlightDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
    const int height = qMax(QFontMetrics(option.font).height(), QFontMetrics(m_mono).height()) + 8;
    return {200, height};
}

void HighlightDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setClipRect(option.rect);
    const bool selected = option.state & QStyle::State_Selected;
    if (selected) {
        const QPalette::ColorGroup group = option.state & QStyle::State_Active ? QPalette::Active : QPalette::Inactive;
        painter->fillRect(option.rect, option.palette.color(group, QPalette::Highlight));
    }
    if (m_model->rowAt(index.row()).match < 0) paintHeader(painter, option, index.row());
    else paintMatch(painter, option, index.row());
    painter->restore();
}

QString HighlightDelegate::relativeDirectory(const QString &directory) const
{
    for (const QString &root : m_roots) {
        if (directory == root) return {};
        if (directory.startsWith(root + QLatin1Char('/'))) return directory.mid(root.size() + 1);
    }
    const QString home = QDir::homePath();
    return directory.startsWith(home) ? QLatin1Char('~') + directory.mid(home.size()) : directory;
}

void HighlightDelegate::paintHeader(QPainter *painter, const QStyleOptionViewItem &option, int row) const
{
    const FileResult *file = m_model->fileAt(row);
    if (!file) return;
    const bool selected = option.state & QStyle::State_Selected;
    const QPalette &palette = option.palette;
    const QColor textColor = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    QColor dimColor = textColor;
    dimColor.setAlphaF(0.55f);

    const QRect rect = option.rect;
    const int iconSize = 16;
    (file->kind == FileKind::Document ? m_documentIcon : m_fileIcon)
        .paint(painter, QRect(rect.left() + kPadding, rect.top() + (rect.height() - iconSize) / 2, iconSize, iconSize));

    // Match count badge on the right.
    int right = rect.right() - kPadding;
    QFont small = option.font;
    small.setPointSizeF(small.pointSizeF() * 0.85);
    if (const int count = file->matchCount(); count > 0) {
        const QString badge = QLocale().toString(count);
        const QFontMetrics metrics(small);
        const int width = metrics.horizontalAdvance(badge) + 12;
        const QRect box(right - width, rect.top() + 4, width, rect.height() - 8);
        QPainterPath path;
        path.addRoundedRect(box, box.height() / 2.0, box.height() / 2.0);
        QColor fill = palette.color(QPalette::Highlight);
        fill.setAlphaF(selected ? 0.0f : 0.25f);
        painter->fillPath(path, selected ? QColor(255, 255, 255, 60) : fill);
        painter->setFont(small);
        painter->setPen(textColor);
        painter->drawText(box, Qt::AlignCenter, badge);
        right = box.left() - 8;
    }

    QFont bold = option.font;
    bold.setBold(true);
    const QFontMetrics boldMetrics(bold);
    const int x = rect.left() + kPadding + iconSize + 6;
    painter->setFont(bold);
    const QRect nameRect(x, rect.top(), right - x, rect.height());
    if (file->nameSpans.isEmpty()) {
        painter->setPen(textColor);
        painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, boldMetrics.elidedText(file->name(), Qt::ElideMiddle, nameRect.width()));
    } else {
        drawSpans(painter, boldMetrics, QRect(x, rect.top(), right - x + kPadding, rect.height()), x, file->name(),
                  file->nameSpans, highlightColor(palette), textColor, Qt::black);
    }

    const int nameWidth = qMin(boldMetrics.horizontalAdvance(file->name()), nameRect.width());
    const int dirX = x + nameWidth + 10;
    if (dirX < right) {
        painter->setFont(small);
        painter->setPen(dimColor);
        const QString directory = relativeDirectory(file->directory());
        painter->drawText(QRect(dirX, rect.top(), right - dirX, rect.height()), Qt::AlignVCenter | Qt::AlignLeft,
                          QFontMetrics(small).elidedText(directory, Qt::ElideMiddle, right - dirX));
    }
}

void HighlightDelegate::paintMatch(QPainter *painter, const QStyleOptionViewItem &option, int row) const
{
    const LineMatch *match = m_model->matchAt(row);
    if (!match) return;
    const bool selected = option.state & QStyle::State_Selected;
    const QPalette &palette = option.palette;
    const QColor textColor = palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
    QColor dimColor = textColor;
    dimColor.setAlphaF(0.5f);

    const QRect rect = option.rect;
    QFont mono = m_mono;
    mono.setPointSizeF(option.font.pointSizeF());
    const QFontMetrics metrics(mono);
    painter->setFont(mono);

    painter->setPen(dimColor);
    painter->drawText(QRect(rect.left(), rect.top(), kLineNumberWidth, rect.height()), Qt::AlignVCenter | Qt::AlignRight,
                      QString::number(match->lineNumber));

    // Leading indentation is dropped so the match is visible in narrow lists.
    const QString &text = match->text;
    qsizetype indent = 0;
    while (indent < text.size() && text.at(indent).isSpace()) ++indent;
    Spans shifted;
    for (const Span &span : match->spans)
        if (span.start >= indent) shifted.append(Span{qint32(span.start - indent), span.length});

    drawSpans(painter, metrics, rect, rect.left() + kLineNumberWidth + 10, text.mid(indent), shifted, highlightColor(palette),
              textColor, Qt::black);
}
