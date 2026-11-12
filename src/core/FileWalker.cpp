#include "FileWalker.h"

#include <QDateTime>
#include <QSet>

#include <filesystem>
#include <fnmatch.h>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace garimpo {

namespace {

QList<QByteArray> toPatterns(const QStringList &list)
{
    QList<QByteArray> patterns;
    for (const QString &item : list) {
        if (item != QLatin1String("*") && item != QLatin1String("*.*")) patterns.append(item.toUtf8());
    }
    return patterns;
}

bool statFile(const fs::path &path, qint64 &size, qint64 &modified)
{
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return false;
    size = info.st_size;
#if defined(__APPLE__)
    modified = qint64(info.st_mtimespec.tv_sec) * 1000 + info.st_mtimespec.tv_nsec / 1000000;
#else
    modified = qint64(info.st_mtim.tv_sec) * 1000 + info.st_mtim.tv_nsec / 1000000;
#endif
    return true;
}

} // namespace

FileWalker::FileWalker(const SearchQuery &query)
    : m_query(query)
    , m_include(toPatterns(query.includeMaskList()))
    , m_exclude(toPatterns(query.excludeMaskList()))
    , m_excludeDirectories(toPatterns(query.excludeDirectoryList()))
{
    if (query.modifiedAfter) m_after = QDateTime(*query.modifiedAfter, QTime(0, 0)).toMSecsSinceEpoch();
    // "Before" includes the whole chosen day.
    if (query.modifiedBefore) m_before = QDateTime(query.modifiedBefore->addDays(1), QTime(0, 0)).toMSecsSinceEpoch();
}

bool FileWalker::matchesAny(const QString &name, const QList<QByteArray> &patterns)
{
    const QByteArray encoded = name.toUtf8();
    for (const QByteArray &pattern : patterns) {
        if (::fnmatch(pattern.constData(), encoded.constData(), FNM_CASEFOLD) == 0) return true;
    }
    return false;
}

bool FileWalker::accepts(const FileCandidate &candidate, const QString &name) const
{
    if (!m_include.isEmpty() && !matchesAny(name, m_include)) return false;
    if (!m_exclude.isEmpty() && matchesAny(name, m_exclude)) return false;
    if (m_query.minSizeKB && candidate.size < *m_query.minSizeKB * 1024) return false;
    if (m_query.maxSizeKB && candidate.size > *m_query.maxSizeKB * 1024) return false;
    if (m_after && candidate.modifiedMSecs < m_after) return false;
    if (m_before && candidate.modifiedMSecs >= m_before) return false;
    return true;
}

void FileWalker::walk(const std::function<bool(const FileCandidate &)> &visit, const std::atomic<bool> *cancel) const
{
    // Only needed when folders overlap; skipped for a single folder to save memory.
    const QStringList roots = m_query.directoryList();
    const bool deduplicate = roots.size() > 1;
    QSet<QString> visited;
    auto cancelled = [cancel] { return cancel && cancel->load(std::memory_order_relaxed); };

    auto offer = [&](const fs::path &path) -> bool {
        FileCandidate candidate;
        if (!statFile(path, candidate.size, candidate.modifiedMSecs)) return true;
        candidate.path = QString::fromStdString(path.string());
        const QString name = QString::fromStdString(path.filename().string());
        if (!accepts(candidate, name)) return true;
        if (deduplicate) {
            if (visited.contains(candidate.path)) return true;
            visited.insert(candidate.path);
        }
        return visit(candidate);
    };

    for (const QString &directory : roots) {
        if (cancelled()) return;
        const fs::path root(directory.toStdString());
        std::error_code error;
        const auto status = fs::status(root, error);
        if (error) continue;
        if (fs::is_regular_file(status)) {
            if (!offer(root)) return;
            continue;
        }
        if (!fs::is_directory(status)) continue;

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error);
        const fs::recursive_directory_iterator end;
        while (!error && it != end) {
            if (cancelled()) return;
            const fs::directory_entry &entry = *it;
            const std::string filename = entry.path().filename().string();
            const bool hidden = !filename.empty() && filename.front() == '.';

            std::error_code typeError;
            // Symbolic links to folders are not followed, which also avoids cycles.
            const bool isDirectory = entry.is_directory(typeError) && !entry.is_symlink(typeError);
            if (isDirectory) {
                const QString name = QString::fromStdString(filename);
                if ((hidden && !m_query.includeHidden) || !m_query.recursive || matchesAny(name, m_excludeDirectories))
                    it.disable_recursion_pending();
            } else if ((!hidden || m_query.includeHidden) && !offer(entry.path())) {
                return;
            }
            it.increment(error);
        }
    }
}

} // namespace garimpo
