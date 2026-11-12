import Foundation

/// Boolean search expression.
///
/// Grammar:
/// ```
/// expr   := or
/// or     := and (("OR" | "|") and)*
/// and    := unary (("AND" | "&")? unary)*     -- adjacent terms are ANDed
/// unary  := ("NOT" | "!" | "-") unary | primary
/// primary:= "(" expr ")" | "quoted phrase" | /regex/ | word
/// ```
public indirect enum SearchExpression: Equatable, Sendable {
    case term(Int)
    case not(SearchExpression)
    case and(SearchExpression, SearchExpression)
    case or(SearchExpression, SearchExpression)

    public func evaluate(_ found: Set<Int>) -> Bool {
        switch self {
        case .term(let index): found.contains(index)
        case .not(let inner): !inner.evaluate(found)
        case .and(let lhs, let rhs): lhs.evaluate(found) && rhs.evaluate(found)
        case .or(let lhs, let rhs): lhs.evaluate(found) || rhs.evaluate(found)
        }
    }

    /// Terms that are not negated; only these get highlighted.
    public var positiveTerms: Set<Int> { collectTerms(negated: false) }

    private func collectTerms(negated: Bool) -> Set<Int> {
        switch self {
        case .term(let index): negated ? [] : [index]
        case .not(let inner): inner.collectTerms(negated: !negated)
        case .and(let lhs, let rhs), .or(let lhs, let rhs):
            lhs.collectTerms(negated: negated).union(rhs.collectTerms(negated: negated))
        }
    }
}

public struct ExpressionTerm: Equatable, Sendable {
    public var text: String
    public var isRegex: Bool
}

public struct ParsedExpression: Equatable, Sendable {
    public var root: SearchExpression
    public var terms: [ExpressionTerm]
}

public enum ExpressionParser {
    enum Token: Equatable {
        case word(String)
        case phrase(String)
        case regex(String)
        case and, or, not, open, close
    }

    public static func parse(_ source: String) throws -> ParsedExpression {
        var parser = Parser(tokens: try tokenize(source))
        let root = try parser.parseOr()
        if parser.position < parser.tokens.count {
            throw MatcherError.invalidExpression("unexpected token near position \(parser.position + 1)")
        }
        return ParsedExpression(root: root, terms: parser.terms)
    }

    static func tokenize(_ source: String) throws -> [Token] {
        var tokens: [Token] = []
        var index = source.startIndex

        func readDelimited(_ delimiter: Character) throws -> String {
            var value = ""
            index = source.index(after: index)
            while index < source.endIndex, source[index] != delimiter {
                // A backslash escapes the delimiter itself; other escapes are kept for the regex engine.
                if source[index] == "\\", source.index(after: index) < source.endIndex,
                   source[source.index(after: index)] == delimiter {
                    index = source.index(after: index)
                }
                value.append(source[index])
                index = source.index(after: index)
            }
            guard index < source.endIndex else {
                throw MatcherError.invalidExpression("missing closing \(delimiter)")
            }
            index = source.index(after: index)
            return value
        }

        while index < source.endIndex {
            let char = source[index]
            switch char {
            case _ where char.isWhitespace:
                index = source.index(after: index)
            case "(":
                tokens.append(.open); index = source.index(after: index)
            case ")":
                tokens.append(.close); index = source.index(after: index)
            case "&":
                tokens.append(.and); index = source.index(after: index)
            case "|":
                tokens.append(.or); index = source.index(after: index)
            case "!":
                tokens.append(.not); index = source.index(after: index)
            case "-":
                tokens.append(.not); index = source.index(after: index)
            case "\"":
                let phrase = try readDelimited("\"")
                if !phrase.isEmpty { tokens.append(.phrase(phrase)) }
            case "/":
                let pattern = try readDelimited("/")
                if !pattern.isEmpty { tokens.append(.regex(pattern)) }
            default:
                var word = ""
                while index < source.endIndex, !source[index].isWhitespace, !"()\"&|".contains(source[index]) {
                    word.append(source[index])
                    index = source.index(after: index)
                }
                switch word.uppercased() {
                case "AND": tokens.append(.and)
                case "OR": tokens.append(.or)
                case "NOT": tokens.append(.not)
                default: tokens.append(.word(word))
                }
            }
        }
        return tokens
    }

    struct Parser {
        let tokens: [Token]
        var position = 0
        var terms: [ExpressionTerm] = []

        init(tokens: [Token]) { self.tokens = tokens }

        var current: Token? { position < tokens.count ? tokens[position] : nil }

        mutating func parseOr() throws -> SearchExpression {
            var lhs = try parseAnd()
            while current == .or {
                position += 1
                lhs = .or(lhs, try parseAnd())
            }
            return lhs
        }

        mutating func parseAnd() throws -> SearchExpression {
            var lhs = try parseUnary()
            while let token = current, token != .or, token != .close {
                if token == .and { position += 1 }
                lhs = .and(lhs, try parseUnary())
            }
            return lhs
        }

        mutating func parseUnary() throws -> SearchExpression {
            if current == .not {
                position += 1
                return .not(try parseUnary())
            }
            return try parsePrimary()
        }

        mutating func parsePrimary() throws -> SearchExpression {
            guard let token = current else { throw MatcherError.invalidExpression("expression ends too early") }
            position += 1
            switch token {
            case .open:
                let inner = try parseOr()
                guard current == .close else { throw MatcherError.invalidExpression("missing )") }
                position += 1
                return inner
            case .word(let text), .phrase(let text):
                return addTerm(ExpressionTerm(text: text, isRegex: false))
            case .regex(let pattern):
                return addTerm(ExpressionTerm(text: pattern, isRegex: true))
            case .and, .or, .not, .close:
                throw MatcherError.invalidExpression("expected a term")
            }
        }

        mutating func addTerm(_ term: ExpressionTerm) -> SearchExpression {
            if let existing = terms.firstIndex(of: term) { return .term(existing) }
            terms.append(term)
            return .term(terms.count - 1)
        }
    }
}
