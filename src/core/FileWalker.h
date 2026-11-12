#pragma once

#include "Query.h"

#include <QString>

#include <atomic>
#include <functional>

namespace garimpo {

struct FileCandidate {
    QString path;
    qint64 size = 0;
    qint64 modifiedMSecs = 0;
};

/// Walks the query folders and reports the files that pass masks and filters.
class FileWalker {
public:
    explicit FileWalker(const SearchQuery &query);

    /// Calls `visit` for each accepted file until it returns false or `cancel` is set.
    void walk(const std::function<bool(const FileCandidate &)> &visit, const std::atomic<bool> *cancel = nullptr) const;

    bool accepts(const FileCandidate &candidate, const QString &name) const;
    static bool matchesAny(const QString &name, const QList<QByteArray> &patterns);

private:
    SearchQuery m_query;
    QList<QByteArray> m_include;
    QList<QByteArray> m_exclude;
    QList<QByteArray> m_excludeDirectories;
    qint64 m_after = 0;
    qint64 m_before = 0;
};

} // namespace garimpo
