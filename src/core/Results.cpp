#include "Results.h"

#include <QJsonArray>

namespace garimpo {

QString FileResult::name() const
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? path : path.mid(slash + 1);
}

QString FileResult::directory() const
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

int FileResult::matchCount() const
{
    int count = 0;
    for (const LineMatch &match : matches) count += qMax(1, int(match.spans.size()));
    return count;
}

QJsonArray spansToJson(const Spans &spans)
{
    QJsonArray array;
    for (const Span &span : spans) array.append(QJsonArray{span.start, span.length});
    return array;
}

Spans spansFromJson(const QJsonValue &value)
{
    Spans spans;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        const QJsonArray pair = item.toArray();
        if (pair.size() == 2) spans.append(Span{pair.at(0).toInt(), pair.at(1).toInt()});
    }
    return spans;
}

QJsonObject FileResult::toJson() const
{
    QJsonArray matchArray;
    for (const LineMatch &match : matches) {
        matchArray.append(QJsonObject{
            {QStringLiteral("line"), match.lineNumber},
            {QStringLiteral("text"), match.text},
            {QStringLiteral("spans"), spansToJson(match.spans)},
        });
    }
    QJsonObject object{
        {QStringLiteral("path"), path},
        {QStringLiteral("size"), size},
        {QStringLiteral("modified"), modifiedMSecs},
        {QStringLiteral("kind"), kind == FileKind::Document ? QStringLiteral("document") : QStringLiteral("text")},
        {QStringLiteral("matches"), matchArray},
    };
    if (!encoding.isEmpty()) object.insert(QStringLiteral("encoding"), encoding);
    if (!nameSpans.isEmpty()) object.insert(QStringLiteral("nameSpans"), spansToJson(nameSpans));
    if (truncated) object.insert(QStringLiteral("truncated"), true);
    return object;
}

FileResult FileResult::fromJson(const QJsonObject &object)
{
    FileResult file;
    file.path = object.value(QStringLiteral("path")).toString();
    file.size = object.value(QStringLiteral("size")).toInteger();
    file.modifiedMSecs = object.value(QStringLiteral("modified")).toInteger();
    file.kind = object.value(QStringLiteral("kind")).toString() == QLatin1String("document") ? FileKind::Document : FileKind::Text;
    file.encoding = object.value(QStringLiteral("encoding")).toString();
    file.nameSpans = spansFromJson(object.value(QStringLiteral("nameSpans")));
    file.truncated = object.value(QStringLiteral("truncated")).toBool();
    const QJsonArray matchArray = object.value(QStringLiteral("matches")).toArray();
    file.matches.reserve(matchArray.size());
    for (const QJsonValue &value : matchArray) {
        const QJsonObject match = value.toObject();
        file.matches.push_back(LineMatch{
            match.value(QStringLiteral("line")).toInt(),
            match.value(QStringLiteral("text")).toString(),
            spansFromJson(match.value(QStringLiteral("spans"))),
        });
    }
    return file;
}

QJsonObject SearchStats::toJson() const
{
    return QJsonObject{
        {QStringLiteral("filesScanned"), filesScanned},
        {QStringLiteral("filesMatched"), filesMatched},
        {QStringLiteral("matchCount"), matchCount},
        {QStringLiteral("skippedBinary"), skippedBinary},
        {QStringLiteral("skippedUnreadable"), skippedUnreadable},
        {QStringLiteral("skippedDocuments"), skippedDocuments},
        {QStringLiteral("duration"), durationSeconds},
        {QStringLiteral("limitReached"), limitReached},
    };
}

SearchStats SearchStats::fromJson(const QJsonObject &object)
{
    SearchStats stats;
    stats.filesScanned = object.value(QStringLiteral("filesScanned")).toInteger();
    stats.filesMatched = object.value(QStringLiteral("filesMatched")).toInteger();
    stats.matchCount = object.value(QStringLiteral("matchCount")).toInteger();
    stats.skippedBinary = object.value(QStringLiteral("skippedBinary")).toInteger();
    stats.skippedUnreadable = object.value(QStringLiteral("skippedUnreadable")).toInteger();
    stats.skippedDocuments = object.value(QStringLiteral("skippedDocuments")).toInteger();
    stats.durationSeconds = object.value(QStringLiteral("duration")).toDouble();
    stats.limitReached = object.value(QStringLiteral("limitReached")).toBool();
    return stats;
}

} // namespace garimpo
