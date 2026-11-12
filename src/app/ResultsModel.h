#pragma once

#include "Results.h"

#include <QAbstractListModel>
#include <QPointer>

#include <vector>

#include "SearchTab.h"

/// Flat list of file headers and match lines for the current tab. Rows are two integers each,
/// and the view only paints what is visible, so large result sets stay light.
class ResultsModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum class Sort { SearchOrder, Path, MatchCount, Modified };

    struct Row {
        int file;
        /// -1 for the file header row.
        int match;
    };

    explicit ResultsModel(QObject *parent = nullptr);

    void setTab(SearchTab *tab);
    SearchTab *tab() const { return m_tab; }
    void setFilter(const QString &filter);
    void setSort(Sort sort);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    Row rowAt(int row) const { return m_rows.at(size_t(row)); }
    const garimpo::FileResult *fileAt(int row) const;
    const garimpo::LineMatch *matchAt(int row) const;
    /// First row of the file containing `row`.
    int headerRowOf(int row) const;
    int rowForFile(int fileIndex) const;

    /// Next or previous match row, wrapping around; files without matches count as one stop.
    int stepMatch(int fromRow, int step) const;
    int stepFile(int fromRow, int step) const;

private:
    void rebuild();
    void appendFiles(int first, int count);
    bool accepts(const garimpo::FileResult &file) const;
    void appendRowsFor(int fileIndex, std::vector<Row> &rows) const;

    QPointer<SearchTab> m_tab;
    std::vector<Row> m_rows;
    std::vector<int> m_headerRows;
    QString m_filter;
    Sort m_sort = Sort::SearchOrder;
};
