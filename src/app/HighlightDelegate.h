#pragma once

#include <QFont>
#include <QIcon>
#include <QStyledItemDelegate>

class ResultsModel;

/// Paints result rows directly: file headers and match lines with highlighted spans.
class HighlightDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    HighlightDelegate(ResultsModel *model, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /// Folders of the current search, used to show paths relative to them.
    void setRoots(const QStringList &roots) { m_roots = roots; }

private:
    void paintHeader(QPainter *painter, const QStyleOptionViewItem &option, int row) const;
    void paintMatch(QPainter *painter, const QStyleOptionViewItem &option, int row) const;
    QString relativeDirectory(const QString &directory) const;

    ResultsModel *m_model;
    QFont m_mono;
    QIcon m_fileIcon;
    QIcon m_documentIcon;
    QStringList m_roots;
};
