#pragma once

#include "Query.h"
#include "Results.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <vector>

namespace garimpo {

/// Where Garimpo keeps its data: ~/Library/Application Support/Garimpo on macOS,
/// ~/.local/share/Garimpo on Linux, or the folder in GARIMPO_HOME.
class AppPaths {
public:
    static QString base();
    static QString historyFile() { return base() + QStringLiteral("/history.json"); }
    static QString sessionFile() { return base() + QStringLiteral("/session.json"); }
    static QString cacheDirectory() { return base() + QStringLiteral("/cache"); }
    static QString backupsDirectory() { return base() + QStringLiteral("/backups"); }
    static qint64 folderSize(const QString &directory);
};

namespace Json {
std::optional<QJsonObject> readObject(const QString &path);
bool writeObject(const QString &path, const QJsonObject &object, bool compact = false);
} // namespace Json

/// A saved search: query plus results, stored as JSON in `.garimpo` files.
struct ResultDocument {
    static constexpr int currentVersion = 1;
    static QString fileExtension() { return QStringLiteral("garimpo"); }

    QString title;
    SearchQuery query;
    QDateTime createdAt = QDateTime::currentDateTime();
    std::vector<FileResult> files;
    SearchStats stats;

    QJsonObject toJson() const;
    static ResultDocument fromJson(const QJsonObject &object);
    bool write(const QString &path, QString *error = nullptr) const;
    static std::optional<ResultDocument> read(const QString &path, QString *error = nullptr);
};

enum class HistoryField { Directories, IncludeMasks, ExcludeMasks, Patterns, Replacements };

struct HistoryEntry {
    QString value;
    bool pinned = false;
    QDateTime lastUsed;
};

/// Recently used values for every form field, plus the last query, kept between launches.
class History {
public:
    static constexpr int defaultLimit = 30;

    std::vector<HistoryEntry> entries(HistoryField field) const;
    QStringList values(HistoryField field) const;

    void remember(const QString &value, HistoryField field);
    void remember(const SearchQuery &query);
    bool isPinned(const QString &value, HistoryField field) const;
    void setPinned(bool pinned, const QString &value, HistoryField field);
    void remove(const QString &value, HistoryField field);
    /// Clears a field but keeps pinned entries.
    void clear(HistoryField field);

    int limit() const { return m_limit; }
    void setLimit(int limit);

    std::optional<SearchQuery> lastQuery;
    QString lastReplacement;

    QJsonObject toJson() const;
    static History fromJson(const QJsonObject &object);
    static History load(const QString &path = AppPaths::historyFile());
    bool save(const QString &path = AppPaths::historyFile()) const;

private:
    void trim(HistoryField field);

    QHash<int, std::vector<HistoryEntry>> m_lists;
    int m_limit = defaultLimit;
};

} // namespace garimpo
