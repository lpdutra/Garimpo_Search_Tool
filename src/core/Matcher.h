#pragma once

#include "Expression.h"
#include "Query.h"
#include "Results.h"

#include <QRegularExpression>
#include <QStringMatcher>

#include <optional>
#include <vector>

namespace garimpo {

/// A range in the whole text.
struct Occurrence {
    qsizetype start;
    qsizetype length;
};

/// One searchable term: literal text (fast path) or a regular expression.
class Term {
public:
    static Term literal(const QString &text, bool caseSensitive, bool wholeWord);
    static Term regex(const QString &pattern, bool caseSensitive, bool wholeWord);

    /// Appends every non-empty occurrence in `text`, stopping after `limit`.
    void findAll(const QString &text, std::vector<Occurrence> &out, qsizetype limit) const;
    bool containsIn(const QString &text) const;

    /// Regex for rewriting; literal terms are escaped.
    QRegularExpression replacementRegex() const;
    bool isLiteral() const { return m_isLiteral; }

private:
    bool acceptsWordBoundaries(const QString &text, qsizetype start, qsizetype length) const;

    bool m_isLiteral = true;
    bool m_wholeWord = false;
    Qt::CaseSensitivity m_caseSensitivity = Qt::CaseInsensitive;
    QString m_text;
    QStringMatcher m_matcher;
    QRegularExpression m_regex;
};

struct MatchResult {
    std::vector<LineMatch> matches;
    bool truncated = false;
    /// The file satisfies the pattern even if no line can be shown (negative-only file expressions).
    bool fileMatches = false;
};

/// Compiled form of a query pattern. Thread-safe for concurrent `match` calls.
class Matcher {
public:
    /// Throws PatternError for an empty or invalid pattern.
    explicit Matcher(const SearchQuery &query);

    MatchResult match(const QString &text, int lineLimit) const;
    std::optional<Spans> matchName(const QString &name) const;

    /// Only literal and regex patterns can be used to replace.
    QRegularExpression replacementRegex() const;
    bool supportsReplace() const { return !m_expression; }

    /// Longest line text kept in a result; longer lines are clipped around the first match.
    static constexpr qsizetype maxStoredLineLength = 1000;

    static LineMatch makeLineMatch(int lineNumber, QStringView line, Spans spans);
    static Spans merge(Spans spans);

private:
    struct Tagged {
        qsizetype start;
        qsizetype length;
        int term;
    };

    MatchResult collectLines(const QString &text, const std::vector<Tagged> &occurrences, int lineLimit,
                             const std::vector<bool> *highlight, bool evaluateLines) const;

    SearchQuery m_query;
    std::vector<Term> m_terms;
    std::optional<Expression> m_expression;
    std::vector<bool> m_positive;
};

} // namespace garimpo
