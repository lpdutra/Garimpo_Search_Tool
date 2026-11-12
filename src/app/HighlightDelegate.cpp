#include "HighlightDelegate.h"

#include "ResultsModel.h"
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>

using namespace garimpo;

namespace {

constexpr int kLineNumberWidth = 56;
constexpr int kPadding = 8;

QColor highlightColor(const QPalette &palette)
{
    return Theme::matchBackground(palette);
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
            QPainterPath path;
            path.addRoundedRect(QRectF(box), 3, 3);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->fillPath(path, mark);
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
{
}

QIcon HighlightDelegate::iconFor(const QString &path) const
{
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    const QString suffix = dot > path.lastIndexOf(QLatin1Char('/')) ? path.mid(dot + 1).toLower() : QString();
    auto it = m_icons.constFind(suffix);
    if (it != m_icons.constEnd()) return *it;
    QIcon icon = m_iconProvider.icon(QFileInfo(path));
    if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    m_icons.insert(suffix, icon);
    return icon;
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
    if (!selected) painter->fillRect(rect, Theme::accentSoft(palette, 0.10));
    const int iconSize = 16;
    iconFor(file->path).paint(painter, QRect(rect.left() + kPadding, rect.top() + (rect.height() - iconSize) / 2, iconSize, iconSize));

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
        path.addRoundedRect(QRectF(box), box.height() / 2.0, box.height() / 2.0);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->fillPath(path, selected ? QColor(255, 255, 255, 70) : Theme::accent(palette));
        painter->setFont(small);
        painter->setPen(Qt::white);
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

    QColor numberColor = selected ? dimColor : Theme::accent(palette);
    if (!selected) numberColor.setAlphaF(0.85f);
    painter->setPen(numberColor);
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
