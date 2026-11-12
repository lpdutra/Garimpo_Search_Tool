#pragma once

#include <QDate>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace garimpo {

enum class PatternMode { Literal, Regex, Expression };
enum class ExpressionScope { Line, File };

QString patternModeTitle(PatternMode mode);

struct SearchQuery {
    /// One or more folders separated by `;`.
    QString directories;
    /// Glob patterns separated by `;`. Empty or `*` accepts every file.
    QString includeMasks = QStringLiteral("*");
    QString excludeMasks;
    QString excludeDirectories = defaultExcludeDirectories();
    bool recursive = true;
    bool includeHidden = false;

    /// Empty pattern lists every file accepted by the masks.
    QString pattern;
    PatternMode mode = PatternMode::Literal;
    bool caseSensitive = false;
    bool wholeWord = false;
    ExpressionScope expressionScope = ExpressionScope::Line;
    /// Matches the pattern against file names instead of contents.
    bool fileNamesOnly = false;
    /// Extracts text from PDF, DOCX, ODT and RTF files before matching.
    bool searchDocuments = false;

    std::optional<qint64> minSizeKB;
    std::optional<qint64> maxSizeKB;
    std::optional<QDate> modifiedAfter;
    std::optional<QDate> modifiedBefore;

    QStringList directoryList() const;
    QStringList includeMaskList() const;
    QStringList excludeMaskList() const;
    QStringList excludeDirectoryList() const;
    QString summary() const;

    QJsonObject toJson() const;
    static SearchQuery fromJson(const QJsonObject &object);

    static QString defaultExcludeDirectories();
    static QStringList splitList(const QString &value);

    bool operator==(const SearchQuery &) const = default;
};

} // namespace garimpo
