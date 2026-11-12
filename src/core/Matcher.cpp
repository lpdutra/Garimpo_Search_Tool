#include "Matcher.h"

#include <algorithm>

namespace garimpo {

namespace {

constexpr qsizetype kOccurrenceLimit = 500000;

bool isWordChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

QString wrapWholeWord(const QString &pattern)
{
    // Lookarounds instead of \b so terms that start or end with punctuation still work.
    return QStringLiteral("(?<![\\p{L}\\p{N}_])(?:%1)(?![\\p{L}\\p{N}_])").arg(pattern);
}

QRegularExpression compile(const QString &pattern, bool caseSensitive)
{
    QRegularExpression::PatternOptions options = QRegularExpression::MultilineOption
        | QRegularExpression::UseUnicodePropertiesOption;
    if (!caseSensitive) options |= QRegularExpression::CaseInsensitiveOption;
    // (*ANYCRLF) makes ^ and $ treat \r\n and \r as line ends too.
    QRegularExpression regex(QStringLiteral("(*ANYCRLF)") + pattern, options);
    if (!regex.isValid()) throw PatternError(QStringLiteral("Invalid regular expression: %1").arg(regex.errorString()));
    regex.optimize();
    return regex;
}

/// Walks forward through a text keeping track of the current line, so line numbers are found
/// without splitting the whole file.
class LineCursor {
public:
    explicit LineCursor(const QString &text) : m_data(reinterpret_cast<const char16_t *>(text.utf16())), m_size(text.size()) {}

    /// Moves to the line containing `position`; positions must not decrease.
    void advanceTo(qsizetype position)
    {
        while (m_scan < position) {
            const char16_t ch = m_data[m_scan];
            if (ch == u'\n' || ch == u'\r') {
                if (ch == u'\r' && m_scan + 1 < m_size && m_data[m_scan + 1] == u'\n') {
                    if (m_scan + 1 >= position) break; // position sits inside "\r\n", still this line
                    ++m_scan;
                }
                ++m_line;
                m_lineStart = m_scan + 1;
            }
            ++m_scan;
        }
    }

    int lineNumber() const { return m_line + 1; }
    qsizetype lineStart() const { return m_lineStart; }

    qsizetype lineEnd() const
    {
        qsizetype end = qMax(m_scan, m_lineStart);
        while (end < m_size && m_data[end] != u'\n' && m_data[end] != u'\r') ++end;
        return end;
    }

private:
    const char16_t *m_data;
    qsizetype m_size;
    qsizetype m_scan = 0;
    qsizetype m_lineStart = 0;
    int m_line = 0;
};

} // namespace

// MARK: Term

Term Term::literal(const QString &text, bool caseSensitive, bool wholeWord)
{
    if (text.isEmpty()) throw PatternError(QStringLiteral("The search pattern is empty."));
    Term term;
    term.m_isLiteral = true;
    term.m_wholeWord = wholeWord;
    term.m_text = text;
    term.m_caseSensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    term.m_matcher = QStringMatcher(text, term.m_caseSensitivity);
    return term;
}

Term Term::regex(const QString &pattern, bool caseSensitive, bool wholeWord)
{
    if (pattern.isEmpty()) throw PatternError(QStringLiteral("The search pattern is empty."));
    Term term;
    term.m_isLiteral = false;
    term.m_wholeWord = wholeWord;
    term.m_text = pattern;
    term.m_caseSensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    term.m_regex = compile(wholeWord ? wrapWholeWord(pattern) : pattern, caseSensitive);
    return term;
}

bool Term::acceptsWordBoundaries(const QString &text, qsizetype start, qsizetype length) const
{
    if (!m_wholeWord) return true;
    if (start > 0 && isWordChar(text.at(start - 1))) return false;
    const qsizetype end = start + length;
    return end >= text.size() || !isWordChar(text.at(end));
}

void Term::findAll(const QString &text, std::vector<Occurrence> &out, qsizetype limit) const
{
    qsizetype found = 0;
    if (m_isLiteral) {
        const qsizetype length = m_text.size();
        qsizetype from = 0;
        while (found < limit) {
            const qsizetype at = m_matcher.indexIn(text, from);
            if (at < 0) break;
            if (acceptsWordBoundaries(text, at, length)) {
                out.push_back({at, length});
                ++found;
                from = at + length;
            } else {
                from = at + 1;
            }
        }
        return;
    }
    auto iterator = m_regex.globalMatch(text);
    while (iterator.hasNext() && found < limit) {
        const QRegularExpressionMatch match = iterator.next();
        if (match.capturedLength() == 0) continue;
        out.push_back({match.capturedStart(), match.capturedLength()});
        ++found;
    }
}

bool Term::containsIn(const QString &text) const
{
    if (m_isLiteral && !m_wholeWord) return m_matcher.indexIn(text) >= 0;
    std::vector<Occurrence> one;
    findAll(text, one, 1);
    return !one.empty();
}

QRegularExpression Term::replacementRegex() const
{
    if (!m_isLiteral) return m_regex;
    const QString escaped = QRegularExpression::escape(m_text);
    return compile(m_wholeWord ? wrapWholeWord(escaped) : escaped, m_caseSensitivity == Qt::CaseSensitive);
}

// MARK: Matcher

Matcher::Matcher(const SearchQuery &query) : m_query(query)
{
    switch (query.mode) {
    case PatternMode::Literal:
        m_terms.push_back(Term::literal(query.pattern, query.caseSensitive, query.wholeWord));
        break;
    case PatternMode::Regex:
        m_terms.push_back(Term::regex(query.pattern, query.caseSensitive, query.wholeWord));
        break;
    case PatternMode::Expression: {
        if (query.pattern.trimmed().isEmpty()) throw PatternError(QStringLiteral("The search pattern is empty."));
        Expression expression = Expression::parse(query.pattern);
        for (const ExpressionTerm &term : expression.terms()) {
            m_terms.push_back(term.isRegex ? Term::regex(term.text, query.caseSensitive, query.wholeWord)
                                           : Term::literal(term.text, query.caseSensitive, query.wholeWord));
        }
        m_positive = expression.positiveTerms();
        m_expression = std::move(expression);
        break;
    }
    }
}

MatchResult Matcher::match(const QString &text, int lineLimit) const
{
    std::vector<Tagged> tagged;
    std::vector<Occurrence> found;
    std::vector<bool> present(m_terms.size(), false);

    for (size_t index = 0; index < m_terms.size(); ++index) {
        found.clear();
        m_terms[index].findAll(text, found, kOccurrenceLimit);
        present[index] = !found.empty();
        for (const Occurrence &occurrence : found) tagged.push_back({occurrence.start, occurrence.length, int(index)});
    }

    if (!m_expression) return collectLines(text, tagged, lineLimit, nullptr, false);

    std::sort(tagged.begin(), tagged.end(), [](const Tagged &a, const Tagged &b) { return a.start < b.start; });

    if (m_query.expressionScope == ExpressionScope::Line) return collectLines(text, tagged, lineLimit, &m_positive, true);

    if (!m_expression->evaluate(present)) return {};
    std::vector<bool> shown(m_terms.size(), false);
    for (size_t index = 0; index < m_terms.size(); ++index) shown[index] = m_positive[index] && present[index];
    MatchResult result = collectLines(text, tagged, lineLimit, &shown, false);
    // Negative-only expressions ("NOT draft") match files without any line to show.
    result.fileMatches = true;
    return result;
}

MatchResult Matcher::collectLines(const QString &text, const std::vector<Tagged> &occurrences, int lineLimit,
                                  const std::vector<bool> *highlight, bool evaluateLines) const
{
    MatchResult result;
    LineCursor cursor(text);
    std::vector<bool> present;
    if (evaluateLines) present.assign(m_terms.size(), false);

    size_t i = 0;
    while (i < occurrences.size()) {
        cursor.advanceTo(occurrences[i].start);
        const qsizetype lineStart = cursor.lineStart();
        const qsizetype lineEnd = cursor.lineEnd();

        Spans spans;
        if (evaluateLines) std::fill(present.begin(), present.end(), false);
        // Gather every occurrence that starts on this line.
        for (; i < occurrences.size() && occurrences[i].start <= lineEnd; ++i) {
            const Tagged &occurrence = occurrences[i];
            if (evaluateLines) present[size_t(occurrence.term)] = true;
            if (highlight && !(*highlight)[size_t(occurrence.term)]) continue;
            // A regex match running past the line end is clipped to the line.
            const qsizetype end = qMin(occurrence.start + occurrence.length, lineEnd);
            if (end > occurrence.start) spans.append(Span{qint32(occurrence.start - lineStart), qint32(end - occurrence.start)});
        }
        if (evaluateLines && !m_expression->evaluate(present)) continue;
        if (spans.isEmpty()) continue;
        if (int(result.matches.size()) >= lineLimit) {
            result.truncated = true;
            break;
        }
        const QStringView line = QStringView(text).sliced(lineStart, lineEnd - lineStart);
        result.matches.push_back(makeLineMatch(cursor.lineNumber(), line, merge(std::move(spans))));
    }
    result.fileMatches = !result.matches.empty();
    return result;
}

std::optional<Spans> Matcher::matchName(const QString &name) const
{
    MatchResult result = match(name, 1);
    if (result.matches.empty()) return std::nullopt;
    return result.matches.front().spans;
}

QRegularExpression Matcher::replacementRegex() const
{
    if (m_expression) throw PatternError(QStringLiteral("Replace works only with Text or Regex patterns."));
    return m_terms.front().replacementRegex();
}

Spans Matcher::merge(Spans spans)
{
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start < b.start; });
    Spans merged;
    for (const Span &span : spans) {
        if (!merged.isEmpty() && span.start <= merged.last().start + merged.last().length) {
            Span &last = merged.last();
            last.length = qMax(last.start + last.length, span.start + span.length) - last.start;
        } else {
            merged.append(span);
        }
    }
    return merged;
}

LineMatch Matcher::makeLineMatch(int lineNumber, QStringView line, Spans spans)
{
    if (line.size() <= maxStoredLineLength || spans.isEmpty())
        return LineMatch{lineNumber, line.toString(), std::move(spans)};

    qsizetype start = qMax<qsizetype>(0, spans.first().start - 100);
    qsizetype end = qMin(line.size(), start + maxStoredLineLength);
    // Do not cut a surrogate pair at either edge.
    if (start > 0 && line.at(start).isLowSurrogate()) --start;
    if (end < line.size() && line.at(end).isLowSurrogate()) --end;

    Spans clipped;
    for (const Span &span : spans) {
        if (span.start >= start && span.start + span.length <= end)
            clipped.append(Span{qint32(span.start - start), span.length});
    }
    return LineMatch{lineNumber, line.sliced(start, end - start).toString(), clipped};
}

} // namespace garimpo
