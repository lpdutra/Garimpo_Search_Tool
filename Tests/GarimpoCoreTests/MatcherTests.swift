import Foundation
import Testing
@testable import GarimpoCore

struct ExpressionParserTests {
    @Test func implicitAndBindsTighterThanOr() throws {
        let parsed = try ExpressionParser.parse("alpha beta OR gamma")
        #expect(parsed.terms.map(\.text) == ["alpha", "beta", "gamma"])
        #expect(parsed.root == .or(.and(.term(0), .term(1)), .term(2)))
    }

    @Test func notParenthesesPhrasesAndRegex() throws {
        let parsed = try ExpressionParser.parse(#"("exact phrase" | /fo+/) AND NOT -skip"#)
        #expect(parsed.terms == [
            ExpressionTerm(text: "exact phrase", isRegex: false),
            ExpressionTerm(text: "fo+", isRegex: true),
            ExpressionTerm(text: "skip", isRegex: false),
        ])
        #expect(parsed.root == .and(.or(.term(0), .term(1)), .not(.not(.term(2)))))
    }

    @Test func symbolsAreOperators() throws {
        let parsed = try ExpressionParser.parse("a & !b")
        #expect(parsed.root == .and(.term(0), .not(.term(1))))
        #expect(parsed.root.positiveTerms == [0])
    }

    @Test func reportsSyntaxErrors() {
        #expect(throws: MatcherError.self) { try ExpressionParser.parse("(alpha") }
        #expect(throws: MatcherError.self) { try ExpressionParser.parse("alpha OR") }
        #expect(throws: MatcherError.self) { try ExpressionParser.parse("\"open") }
    }
}

struct MatcherTests {
    func query(_ pattern: String, mode: PatternMode = .literal, configure: (inout SearchQuery) -> Void = { _ in }) -> SearchQuery {
        var query = SearchQuery()
        query.pattern = pattern
        query.mode = mode
        configure(&query)
        return query
    }

    @Test func literalIsCaseInsensitiveByDefault() throws {
        let matcher = try Matcher(query: query("hello"))
        let result = matcher.match(lines: TextLines.split("Hello world\nnothing\nsay HELLO"), limit: 100)
        #expect(result.matches.map(\.lineNumber) == [1, 3])
        #expect(result.matches[1].spans == [TextSpan(location: 4, length: 5)])
    }

    @Test func literalEscapesRegexCharacters() throws {
        let matcher = try Matcher(query: query("a.b(c)"))
        #expect(matcher.match(lines: TextLines.split("axb(c)\na.b(c)"), limit: 10).matches.map(\.lineNumber) == [2])
    }

    @Test func wholeWordAndCaseSensitive() throws {
        let matcher = try Matcher(query: query("Find") { $0.wholeWord = true; $0.caseSensitive = true })
        let lines = TextLines.split("Find it\nFinder\nfind\n(Find)")
        #expect(matcher.match(lines: lines, limit: 10).matches.map(\.lineNumber) == [1, 4])
    }

    @Test func regexWithAccents() throws {
        let matcher = try Matcher(query: query("situa[cç][aã]o", mode: .regex))
        #expect(matcher.match(lines: TextLines.split("Situação\nsituacao\nsituation"), limit: 10).matches.count == 2)
    }

    @Test func invalidRegexThrows() {
        #expect(throws: MatcherError.self) { try Matcher(query: query("(", mode: .regex)) }
    }

    @Test func expressionLineScope() throws {
        let matcher = try Matcher(query: query("customer AND NOT test", mode: .expression))
        let lines = TextLines.split("customer id\ncustomer test\nother")
        let result = matcher.match(lines: lines, limit: 10)
        #expect(result.matches.map(\.lineNumber) == [1])
        #expect(result.matches[0].spans == [TextSpan(location: 0, length: 8)])
    }

    @Test func expressionFileScope() throws {
        let matcher = try Matcher(query: query("invoice AND total", mode: .expression) { $0.expressionScope = .file })
        let both = TextLines.split("invoice header\nline\ngrand total")
        #expect(matcher.match(lines: both, limit: 10).matches.map(\.lineNumber) == [1, 3])
        #expect(matcher.match(lines: TextLines.split("invoice only"), limit: 10).matches.isEmpty)
    }

    @Test func limitTruncates() throws {
        let matcher = try Matcher(query: query("x"))
        let result = matcher.match(lines: TextLines.split("x\nx\nx"), limit: 2)
        #expect(result.matches.count == 2)
        #expect(result.truncated)
    }

    @Test func longLinesAreClippedAroundFirstMatch() throws {
        let line = String(repeating: "a", count: 5000) + "needle" + String(repeating: "b", count: 5000)
        let matcher = try Matcher(query: query("needle"))
        let match = try #require(matcher.match(lines: TextLines.split(line), limit: 1).matches.first)
        #expect(match.text.utf16.count == LineMatch.maxStoredLength)
        let span = try #require(match.spans.first)
        #expect((match.text as NSString).substring(with: span.nsRange) == "needle")
    }

    @Test func fileNames() throws {
        let matcher = try Matcher(query: query("report"))
        #expect(matcher.nameSpans("Monthly-Report.pdf") == [TextSpan(location: 8, length: 6)])
        #expect(matcher.nameSpans("notes.txt") == nil)
    }

    @Test func mergeOverlappingSpans() {
        let merged = Matcher.merge([TextSpan(location: 5, length: 3), TextSpan(location: 0, length: 2), TextSpan(location: 6, length: 4)])
        #expect(merged == [TextSpan(location: 0, length: 2), TextSpan(location: 5, length: 5)])
    }
}
