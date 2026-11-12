#pragma once

#include <QFileIconProvider>
#include <QFont>
#include <QHash>
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
    /// System icon for the file type, cached per extension.
    QIcon iconFor(const QString &path) const;

    ResultsModel *m_model;
    QFont m_mono;
    QFileIconProvider m_iconProvider;
    mutable QHash<QString, QIcon> m_icons;
    QStringList m_roots;
};
