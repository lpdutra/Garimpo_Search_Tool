#include "Query.h"

#include <QDir>

namespace garimpo {

namespace {

QString modeKey(PatternMode mode)
{
    switch (mode) {
    case PatternMode::Literal: return QStringLiteral("literal");
    case PatternMode::Regex: return QStringLiteral("regex");
    case PatternMode::Expression: return QStringLiteral("expression");
    }
    return QStringLiteral("literal");
}

PatternMode modeFromKey(const QString &key)
{
    if (key == QLatin1String("regex")) return PatternMode::Regex;
    if (key == QLatin1String("expression")) return PatternMode::Expression;
    return PatternMode::Literal;
}

QString expandTilde(const QString &path)
{
    if (path == QLatin1String("~")) return QDir::homePath();
    if (path.startsWith(QLatin1String("~/"))) return QDir::homePath() + path.mid(1);
    return path;
}

} // namespace

QString patternModeTitle(PatternMode mode)
{
    switch (mode) {
    case PatternMode::Literal: return QStringLiteral("Text");
    case PatternMode::Regex: return QStringLiteral("Regex");
    case PatternMode::Expression: return QStringLiteral("Expression");
    }
    return {};
}

QString SearchQuery::defaultExcludeDirectories()
{
    return QStringLiteral(".git;.svn;.hg;node_modules;.build;build;bin;obj;.idea;.vscode");
}

QStringList SearchQuery::splitList(const QString &value)
{
    QStringList result;
    const auto parts = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) result.append(trimmed);
    }
    return result;
}

QStringList SearchQuery::directoryList() const
{
    QStringList result;
    for (const QString &directory : splitList(directories)) {
        QString path = QDir::cleanPath(expandTilde(directory));
        if (path.size() > 1 && path.endsWith(QLatin1Char('/'))) path.chop(1);
        result.append(path);
    }
    return result;
}

QStringList SearchQuery::includeMaskList() const { return splitList(includeMasks); }
QStringList SearchQuery::excludeMaskList() const { return splitList(excludeMasks); }
QStringList SearchQuery::excludeDirectoryList() const { return splitList(excludeDirectories); }

QString SearchQuery::summary() const
{
    if (!pattern.isEmpty()) return pattern;
    return includeMasks.trimmed().isEmpty() ? QStringLiteral("All files") : includeMasks;
}

QJsonObject SearchQuery::toJson() const
{
    QJsonObject object{
        {QStringLiteral("directories"), directories},
        {QStringLiteral("includeMasks"), includeMasks},
        {QStringLiteral("excludeMasks"), excludeMasks},
        {QStringLiteral("excludeDirectories"), excludeDirectories},
        {QStringLiteral("recursive"), recursive},
        {QStringLiteral("includeHidden"), includeHidden},
        {QStringLiteral("pattern"), pattern},
        {QStringLiteral("mode"), modeKey(mode)},
        {QStringLiteral("caseSensitive"), caseSensitive},
        {QStringLiteral("wholeWord"), wholeWord},
        {QStringLiteral("expressionScope"), expressionScope == ExpressionScope::File ? QStringLiteral("file") : QStringLiteral("line")},
        {QStringLiteral("fileNamesOnly"), fileNamesOnly},
        {QStringLiteral("searchDocuments"), searchDocuments},
    };
    if (minSizeKB) object.insert(QStringLiteral("minSizeKB"), *minSizeKB);
    if (maxSizeKB) object.insert(QStringLiteral("maxSizeKB"), *maxSizeKB);
    if (modifiedAfter) object.insert(QStringLiteral("modifiedAfter"), modifiedAfter->toString(Qt::ISODate));
    if (modifiedBefore) object.insert(QStringLiteral("modifiedBefore"), modifiedBefore->toString(Qt::ISODate));
    return object;
}

SearchQuery SearchQuery::fromJson(const QJsonObject &object)
{
    SearchQuery query;
    query.directories = object.value(QStringLiteral("directories")).toString();
    query.includeMasks = object.value(QStringLiteral("includeMasks")).toString(query.includeMasks);
    query.excludeMasks = object.value(QStringLiteral("excludeMasks")).toString();
    query.excludeDirectories = object.value(QStringLiteral("excludeDirectories")).toString(query.excludeDirectories);
    query.recursive = object.value(QStringLiteral("recursive")).toBool(true);
    query.includeHidden = object.value(QStringLiteral("includeHidden")).toBool();
    query.pattern = object.value(QStringLiteral("pattern")).toString();
    query.mode = modeFromKey(object.value(QStringLiteral("mode")).toString());
    query.caseSensitive = object.value(QStringLiteral("caseSensitive")).toBool();
    query.wholeWord = object.value(QStringLiteral("wholeWord")).toBool();
    query.expressionScope = object.value(QStringLiteral("expressionScope")).toString() == QLatin1String("file")
        ? ExpressionScope::File : ExpressionScope::Line;
    query.fileNamesOnly = object.value(QStringLiteral("fileNamesOnly")).toBool();
    query.searchDocuments = object.value(QStringLiteral("searchDocuments")).toBool();
    if (object.contains(QStringLiteral("minSizeKB"))) query.minSizeKB = object.value(QStringLiteral("minSizeKB")).toInteger();
    if (object.contains(QStringLiteral("maxSizeKB"))) query.maxSizeKB = object.value(QStringLiteral("maxSizeKB")).toInteger();
    const QDate after = QDate::fromString(object.value(QStringLiteral("modifiedAfter")).toString(), Qt::ISODate);
    const QDate before = QDate::fromString(object.value(QStringLiteral("modifiedBefore")).toString(), Qt::ISODate);
    if (after.isValid()) query.modifiedAfter = after;
    if (before.isValid()) query.modifiedBefore = before;
    return query;
}

} // namespace garimpo
