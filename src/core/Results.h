#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVarLengthArray>

#include <vector>

namespace garimpo {

/// UTF-16 range inside a line.
struct Span {
    qint32 start = 0;
    qint32 length = 0;
    bool operator==(const Span &) const = default;
};

/// Most lines have one or two matches, so they stay inline without a heap allocation.
using Spans = QVarLengthArray<Span, 2>;

struct LineMatch {
    /// 1-based line number.
    qint32 lineNumber = 0;
    /// Line text, clipped around the first match when the line is very long.
    QString text;
    Spans spans;

    /// 1-based column of the first match.
    int column() const { return spans.isEmpty() ? 1 : spans.first().start + 1; }
};

enum class FileKind : quint8 { Text, Document };

struct FileResult {
    QString path;
    qint64 size = 0;
    qint64 modifiedMSecs = 0;
    FileKind kind = FileKind::Text;
    /// Static string such as "UTF-8"; empty for documents.
    QString encoding;
    std::vector<LineMatch> matches;
    /// Matches inside the file name when searching names only.
    Spans nameSpans;
    /// The match list was cut by the per-file or total limit.
    bool truncated = false;

    QString name() const;
    QString directory() const;
    int matchCount() const;
    QDateTime modified() const { return QDateTime::fromMSecsSinceEpoch(modifiedMSecs); }

    QJsonObject toJson() const;
    static FileResult fromJson(const QJsonObject &object);
};

struct SearchStats {
    qint64 filesScanned = 0;
    qint64 filesMatched = 0;
    qint64 matchCount = 0;
    qint64 skippedBinary = 0;
    qint64 skippedUnreadable = 0;
    qint64 skippedDocuments = 0;
    double durationSeconds = 0;
    bool limitReached = false;

    QJsonObject toJson() const;
    static SearchStats fromJson(const QJsonObject &object);
};

QJsonArray spansToJson(const Spans &spans);
Spans spansFromJson(const QJsonValue &value);

} // namespace garimpo
