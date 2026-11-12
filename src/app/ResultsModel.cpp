#include "ResultsModel.h"

#include "SearchTab.h"

#include <algorithm>

using namespace garimpo;

ResultsModel::ResultsModel(QObject *parent) : QAbstractListModel(parent) {}

void ResultsModel::setTab(SearchTab *tab)
{
    if (m_tab) disconnect(m_tab, nullptr, this, nullptr);
    m_tab = tab;
    if (tab) {
        connect(tab, &SearchTab::resultsReset, this, &ResultsModel::rebuild);
        connect(tab, &SearchTab::filesAppended, this, &ResultsModel::appendFiles);
    }
    rebuild();
}

void ResultsModel::setFilter(const QString &filter)
{
    if (filter.trimmed() == m_filter) return;
    m_filter = filter.trimmed();
    rebuild();
}

void ResultsModel::setSort(Sort sort)
{
    if (sort == m_sort) return;
    m_sort = sort;
    rebuild();
}

bool ResultsModel::accepts(const FileResult &file) const
{
    return m_filter.isEmpty() || file.path.contains(m_filter, Qt::CaseInsensitive);
}

void ResultsModel::appendRowsFor(int fileIndex, std::vector<Row> &rows) const
{
    rows.push_back({fileIndex, -1});
    const auto &matches = m_tab->files()[size_t(fileIndex)].matches;
    for (int match = 0; match < int(matches.size()); ++match) rows.push_back({fileIndex, match});
}

void ResultsModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    m_headerRows.clear();
    if (m_tab) {
        const auto &files = m_tab->files();
        std::vector<int> order;
        order.reserve(files.size());
        for (int i = 0; i < int(files.size()); ++i)
            if (accepts(files[size_t(i)])) order.push_back(i);

        switch (m_sort) {
        case Sort::SearchOrder:
            break;
        case Sort::Path:
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                return QString::compare(files[size_t(a)].path, files[size_t(b)].path, Qt::CaseInsensitive) < 0;
            });
            break;
        case Sort::MatchCount:
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return files[size_t(a)].matchCount() > files[size_t(b)].matchCount(); });
            break;
        case Sort::Modified:
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return files[size_t(a)].modifiedMSecs > files[size_t(b)].modifiedMSecs; });
            break;
        }
        for (int index : order) {
            m_headerRows.push_back(int(m_rows.size()));
            appendRowsFor(index, m_rows);
        }
    }
    m_rows.shrink_to_fit();
    endResetModel();
}

void ResultsModel::appendFiles(int first, int count)
{
    // Sorted views are rebuilt; search order just grows at the end.
    if (m_sort != Sort::SearchOrder) {
        rebuild();
        return;
    }
    std::vector<Row> added;
    std::vector<int> headers;
    for (int i = first; i < first + count; ++i) {
        if (!accepts(m_tab->files()[size_t(i)])) continue;
        headers.push_back(int(m_rows.size() + added.size()));
        appendRowsFor(i, added);
    }
    if (added.empty()) return;
    beginInsertRows({}, int(m_rows.size()), int(m_rows.size() + added.size()) - 1);
    m_rows.insert(m_rows.end(), added.begin(), added.end());
    m_headerRows.insert(m_headerRows.end(), headers.begin(), headers.end());
    endInsertRows();
}

int ResultsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ResultsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_tab) return {};
    if (role == Qt::ToolTipRole) {
        if (const FileResult *file = fileAt(index.row())) return file->path;
    }
    if (role == Qt::DisplayRole) {
        if (const LineMatch *match = matchAt(index.row())) return match->text;
        if (const FileResult *file = fileAt(index.row())) return file->path;
    }
    return {};
}

const FileResult *ResultsModel::fileAt(int row) const
{
    if (!m_tab || row < 0 || row >= int(m_rows.size())) return nullptr;
    const int file = m_rows[size_t(row)].file;
    if (file < 0 || file >= int(m_tab->files().size())) return nullptr;
    return &m_tab->files()[size_t(file)];
}

const LineMatch *ResultsModel::matchAt(int row) const
{
    const FileResult *file = fileAt(row);
    if (!file) return nullptr;
    const int match = m_rows[size_t(row)].match;
    if (match < 0 || match >= int(file->matches.size())) return nullptr;
    return &file->matches[size_t(match)];
}

int ResultsModel::headerRowOf(int row) const
{
    auto it = std::upper_bound(m_headerRows.begin(), m_headerRows.end(), row);
    return it == m_headerRows.begin() ? 0 : *(it - 1);
}

int ResultsModel::rowForFile(int fileIndex) const
{
    for (int header : m_headerRows)
        if (m_rows[size_t(header)].file == fileIndex) return header;
    return -1;
}

int ResultsModel::stepMatch(int fromRow, int step) const
{
    const int count = int(m_rows.size());
    if (count == 0) return -1;
    auto isStop = [&](int row) {
        const Row &r = m_rows[size_t(row)];
        if (r.match >= 0) return true;
        // A header is a stop only when its file has no match lines.
        return row + 1 >= count || m_rows[size_t(row) + 1].match < 0;
    };
    int row = fromRow < 0 ? (step > 0 ? -1 : count) : fromRow;
    for (int tries = 0; tries < count; ++tries) {
        row = (row + step + count) % count;
        if (isStop(row)) return row;
    }
    return -1;
}

int ResultsModel::stepFile(int fromRow, int step) const
{
    if (m_headerRows.empty()) return -1;
    const int headers = int(m_headerRows.size());
    int current = -1;
    if (fromRow >= 0) {
        const int header = headerRowOf(fromRow);
        current = int(std::lower_bound(m_headerRows.begin(), m_headerRows.end(), header) - m_headerRows.begin());
    }
    const int next = current < 0 ? (step > 0 ? 0 : headers - 1) : (current + step + headers) % headers;
    const int header = m_headerRows[size_t(next)];
    // Land on the first match of the file when it has one.
    return header + 1 < int(m_rows.size()) && m_rows[size_t(header) + 1].match >= 0 ? header + 1 : header;
}
