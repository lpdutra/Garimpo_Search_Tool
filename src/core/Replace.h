#pragma once

#include "Query.h"
#include "Storage.h"

#include <QRegularExpression>
#include <QString>

#include <optional>
#include <vector>

namespace garimpo {

struct LineChange {
    int lineNumber = 0;
    QString original;
    QString replaced;
    bool included = true;
};

struct FileChange {
    QString path;
    QString encoding;
    std::vector<LineChange> lines;
    bool included = true;

    int selectedLineCount() const;
};

struct Problem {
    QString path;
    QString reason;
};

struct ReplacePlan {
    SearchQuery query;
    QString replacement;
    std::vector<FileChange> files;
    std::vector<Problem> skipped;

    int selectedLineCount() const;
    int selectedFileCount() const;
};

/// Record of one replace run, stored with its backups so it can be undone.
struct ReplaceBatch {
    struct Entry {
        QString path;
        QString backupName;
        /// Hash of the written content, to detect edits made after the replace.
        QString writtenHash;
        int changedLines = 0;
    };
    QString id;
    QDateTime date;
    QString pattern;
    QString replacement;
    std::vector<Entry> entries;

    int changedLines() const;
    QJsonObject toJson() const;
    static ReplaceBatch fromJson(const QJsonObject &object);
};

struct ReplaceOutcome {
    std::optional<ReplaceBatch> batch;
    std::vector<Problem> failures;
};

struct UndoOutcome {
    QStringList restored;
    /// Files left alone because they were edited after the replace.
    QStringList modifiedSince;
    std::vector<Problem> failures;
};

class Replacer {
public:
    /// Preview of every line that would change; nothing is written. Throws PatternError.
    static ReplacePlan plan(const SearchQuery &query, const QString &replacement, const QStringList &paths);

    /// Writes the selected lines, backing up every file first.
    static ReplaceOutcome execute(const ReplacePlan &plan, const QString &backupsRoot = AppPaths::backupsDirectory());

    static std::vector<ReplaceBatch> batches(const QString &backupsRoot = AppPaths::backupsDirectory());
    static std::optional<ReplaceBatch> lastBatch(const QString &backupsRoot = AppPaths::backupsDirectory());
    /// Restores a batch and deletes it. Files edited after the replace are kept unless `force` is set.
    static UndoOutcome undo(const ReplaceBatch &batch, bool force = false, const QString &backupsRoot = AppPaths::backupsDirectory());
    static void clearBackups(const QString &backupsRoot = AppPaths::backupsDirectory());

    /// Replaces every match in `text`. Regex templates accept $0-$99, ${name} and $$.
    static QString apply(const QRegularExpression &regex, const QString &text, const QString &replacement, bool literal);
};

} // namespace garimpo
