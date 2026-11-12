import Foundation

public enum MatcherError: LocalizedError, Equatable {
    case emptyPattern
    case invalidRegex(String)
    case invalidExpression(String)
    case replaceNotSupported

    public var errorDescription: String? {
        switch self {
        case .emptyPattern: "The search pattern is empty."
        case .invalidRegex(let message): "Invalid regular expression: \(message)"
        case .invalidExpression(let message): "Invalid search expression: \(message)"
        case .replaceNotSupported: "Replace works only with Text or Regex patterns."
        }
    }
}

/// A single compiled term, matched line by line.
public struct TermMatcher: @unchecked Sendable {
    public let regex: NSRegularExpression

    public init(literal: String, caseSensitive: Bool, wholeWord: Bool) throws {
        try self.init(regex: NSRegularExpression.escapedPattern(for: literal), caseSensitive: caseSensitive, wholeWord: wholeWord)
    }

    public init(regex pattern: String, caseSensitive: Bool, wholeWord: Bool) throws {
        guard !pattern.isEmpty else { throw MatcherError.emptyPattern }
        // Lookarounds instead of \b so terms that start or end with punctuation still work.
        let source = wholeWord ? "(?<![\\p{L}\\p{N}_])(?:\(pattern))(?![\\p{L}\\p{N}_])" : pattern
        var options: NSRegularExpression.Options = []
        if !caseSensitive { options.insert(.caseInsensitive) }
        do {
            regex = try NSRegularExpression(pattern: source, options: options)
        } catch {
            throw MatcherError.invalidRegex(pattern)
        }
    }

    public func spans(in text: String) -> [TextSpan] {
        let range = NSRange(text.startIndex..., in: text)
        return regex.matches(in: text, range: range)
            .filter { $0.range.length > 0 }
            .map { TextSpan($0.range) }
    }

    public func matches(_ text: String) -> Bool {
        regex.firstMatch(in: text, range: NSRange(text.startIndex..., in: text)) != nil
    }
}

/// Compiled form of a `SearchQuery` pattern.
public struct Matcher: Sendable {
    public let query: SearchQuery
    private let expression: SearchExpression?
    private let terms: [TermMatcher]

    public init(query: SearchQuery) throws {
        self.query = query
        switch query.mode {
        case .literal:
            terms = [try TermMatcher(literal: query.pattern, caseSensitive: query.caseSensitive, wholeWord: query.wholeWord)]
            expression = nil
        case .regex:
            terms = [try TermMatcher(regex: query.pattern, caseSensitive: query.caseSensitive, wholeWord: query.wholeWord)]
            expression = nil
        case .expression:
            let parsed = try ExpressionParser.parse(query.pattern)
            terms = try parsed.terms.map { term in
                term.isRegex
                    ? try TermMatcher(regex: term.text, caseSensitive: query.caseSensitive, wholeWord: query.wholeWord)
                    : try TermMatcher(literal: term.text, caseSensitive: query.caseSensitive, wholeWord: query.wholeWord)
            }
            expression = parsed.root
        }
    }

    /// Finds matching lines. `limit` caps how many lines are returned.
    public func match(lines: [TextLine], limit: Int) -> (matches: [LineMatch], truncated: Bool) {
        guard let expression else {
            return collect(lines: lines, limit: limit) { line in
                let spans = terms[0].spans(in: line)
                return spans.isEmpty ? nil : spans
            }
        }

        let positive = expression.positiveTerms
        switch query.expressionScope {
        case .line:
            return collect(lines: lines, limit: limit) { line in
                var found = Set<Int>()
                for (index, term) in terms.enumerated() where term.matches(line) { found.insert(index) }
                guard expression.evaluate(found) else { return nil }
                return highlight(line, terms: positive.intersection(found))
            }
        case .file:
            var found = Set<Int>()
            for (index, term) in terms.enumerated() where lines.contains(where: { term.matches($0.content) }) {
                found.insert(index)
            }
            guard expression.evaluate(found) else { return ([], false) }
            let shown = positive.intersection(found)
            if shown.isEmpty {
                // Purely negative expressions ("NOT foo") match whole files without any line to show.
                return ([], false)
            }
            return collect(lines: lines, limit: limit) { line in
                let spans = highlight(line, terms: shown)
                return spans.isEmpty ? nil : spans
            }
        }
    }

    /// True when the whole file satisfies the pattern, even if no line can be highlighted.
    public func fileMatches(lines: [TextLine]) -> Bool {
        guard let expression, query.expressionScope == .file else {
            return !match(lines: lines, limit: 1).matches.isEmpty
        }
        var found = Set<Int>()
        for (index, term) in terms.enumerated() where lines.contains(where: { term.matches($0.content) }) {
            found.insert(index)
        }
        return expression.evaluate(found)
    }

    /// Matches a file name; used when searching names only.
    public func nameSpans(_ name: String) -> [TextSpan]? {
        guard let expression else {
            let spans = terms[0].spans(in: name)
            return spans.isEmpty ? nil : spans
        }
        var found = Set<Int>()
        for (index, term) in terms.enumerated() where term.matches(name) { found.insert(index) }
        guard expression.evaluate(found) else { return nil }
        return highlight(name, terms: expression.positiveTerms.intersection(found))
    }

    /// Regex used to rewrite text; only literal and regex modes support replace.
    public func replacementRegex() throws -> NSRegularExpression {
        guard expression == nil else { throw MatcherError.replaceNotSupported }
        return terms[0].regex
    }

    private func highlight(_ text: String, terms indices: Set<Int>) -> [TextSpan] {
        let spans = indices.flatMap { terms[$0].spans(in: text) }
        return Self.merge(spans)
    }

    private func collect(lines: [TextLine], limit: Int, spans: (String) -> [TextSpan]?) -> ([LineMatch], Bool) {
        var result: [LineMatch] = []
        for (index, line) in lines.enumerated() {
            guard let found = spans(line.content) else { continue }
            if result.count >= limit { return (result, true) }
            result.append(LineMatch.make(lineNumber: index + 1, text: line.content, spans: found))
        }
        return (result, false)
    }

    static func merge(_ spans: [TextSpan]) -> [TextSpan] {
        let sorted = spans.sorted { $0.location < $1.location }
        var merged: [TextSpan] = []
        for span in sorted {
            if let last = merged.last, span.location <= last.location + last.length {
                let end = max(last.location + last.length, span.location + span.length)
                merged[merged.count - 1].length = end - last.location
            } else {
                merged.append(span)
            }
        }
        return merged
    }
}

extension LineMatch {
    /// Longest line kept in results; long minified lines are cut around the first match.
    static let maxStoredLength = 1000

    static func make(lineNumber: Int, text: String, spans: [TextSpan]) -> LineMatch {
        let utf16Count = text.utf16.count
        guard utf16Count > maxStoredLength, let first = spans.first else {
            return LineMatch(lineNumber: lineNumber, text: text, spans: spans)
        }
        let start = max(0, first.location - 100)
        let end = min(utf16Count, start + maxStoredLength)
        let ns = text as NSString
        // Avoid cutting through a surrogate pair at either edge.
        let window = ns.rangeOfComposedCharacterSequences(for: NSRange(location: start, length: end - start))
        let clipped = spans.compactMap { span -> TextSpan? in
            guard span.location >= window.location, span.location + span.length <= window.location + window.length else { return nil }
            return TextSpan(location: span.location - window.location, length: span.length)
        }
        return LineMatch(lineNumber: lineNumber, text: ns.substring(with: window), spans: clipped)
    }
}
