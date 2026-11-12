#include "Expression.h"

namespace garimpo {

namespace {

struct Token {
    enum Kind { Word, Phrase, Regex, And, Or, Not, Open, Close } kind;
    QString text;
};

std::vector<Token> tokenize(const QString &source)
{
    std::vector<Token> tokens;
    qsizetype i = 0;
    const qsizetype size = source.size();

    auto readDelimited = [&](QChar delimiter) {
        QString value;
        ++i;
        while (i < size && source.at(i) != delimiter) {
            // A backslash escapes the delimiter; other escapes are kept for the regex engine.
            if (source.at(i) == QLatin1Char('\\') && i + 1 < size && source.at(i + 1) == delimiter) ++i;
            value.append(source.at(i));
            ++i;
        }
        if (i >= size) throw PatternError(QStringLiteral("missing closing %1").arg(delimiter));
        ++i;
        return value;
    };

    while (i < size) {
        const QChar ch = source.at(i);
        if (ch.isSpace()) {
            ++i;
        } else if (ch == QLatin1Char('(')) {
            tokens.push_back({Token::Open, {}}); ++i;
        } else if (ch == QLatin1Char(')')) {
            tokens.push_back({Token::Close, {}}); ++i;
        } else if (ch == QLatin1Char('&')) {
            tokens.push_back({Token::And, {}}); ++i;
        } else if (ch == QLatin1Char('|')) {
            tokens.push_back({Token::Or, {}}); ++i;
        } else if (ch == QLatin1Char('!') || ch == QLatin1Char('-')) {
            tokens.push_back({Token::Not, {}}); ++i;
        } else if (ch == QLatin1Char('"')) {
            const QString phrase = readDelimited(QLatin1Char('"'));
            if (!phrase.isEmpty()) tokens.push_back({Token::Phrase, phrase});
        } else if (ch == QLatin1Char('/')) {
            const QString pattern = readDelimited(QLatin1Char('/'));
            if (!pattern.isEmpty()) tokens.push_back({Token::Regex, pattern});
        } else {
            QString word;
            while (i < size && !source.at(i).isSpace() && !QStringLiteral("()\"&|").contains(source.at(i))) {
                word.append(source.at(i));
                ++i;
            }
            const QString upper = word.toUpper();
            if (upper == QLatin1String("AND")) tokens.push_back({Token::And, {}});
            else if (upper == QLatin1String("OR")) tokens.push_back({Token::Or, {}});
            else if (upper == QLatin1String("NOT")) tokens.push_back({Token::Not, {}});
            else tokens.push_back({Token::Word, word});
        }
    }
    return tokens;
}

} // namespace

class ExpressionParser {
public:
    ExpressionParser(Expression &expression, std::vector<Token> tokens) : m_expression(expression), m_tokens(std::move(tokens)) {}

    int parseOr()
    {
        int left = parseAnd();
        while (is(Token::Or)) {
            ++m_position;
            left = add({Expression::Node::Or, left, parseAnd()});
        }
        return left;
    }

    int parseAnd()
    {
        int left = parseUnary();
        while (m_position < m_tokens.size() && !is(Token::Or) && !is(Token::Close)) {
            if (is(Token::And)) ++m_position;
            left = add({Expression::Node::And, left, parseUnary()});
        }
        return left;
    }

    int parseUnary()
    {
        if (is(Token::Not)) {
            ++m_position;
            return add({Expression::Node::Not, parseUnary(), -1});
        }
        return parsePrimary();
    }

    int parsePrimary()
    {
        if (m_position >= m_tokens.size()) throw PatternError(QStringLiteral("expression ends too early"));
        const Token token = m_tokens[m_position++];
        switch (token.kind) {
        case Token::Open: {
            const int inner = parseOr();
            if (!is(Token::Close)) throw PatternError(QStringLiteral("missing )"));
            ++m_position;
            return inner;
        }
        case Token::Word:
        case Token::Phrase:
            return addTerm({token.text, false});
        case Token::Regex:
            return addTerm({token.text, true});
        default:
            throw PatternError(QStringLiteral("expected a term"));
        }
    }

    size_t position() const { return m_position; }
    size_t tokenCount() const { return m_tokens.size(); }

private:
    bool is(Token::Kind kind) const { return m_position < m_tokens.size() && m_tokens[m_position].kind == kind; }

    int add(Expression::Node node)
    {
        m_expression.m_nodes.push_back(node);
        return int(m_expression.m_nodes.size() - 1);
    }

    int addTerm(const ExpressionTerm &term)
    {
        auto &terms = m_expression.m_terms;
        auto existing = std::find(terms.begin(), terms.end(), term);
        int index = int(std::distance(terms.begin(), existing));
        if (existing == terms.end()) terms.push_back(term);
        return add({Expression::Node::Term, index, -1});
    }

    Expression &m_expression;
    std::vector<Token> m_tokens;
    size_t m_position = 0;
};

Expression Expression::parse(const QString &source)
{
    Expression expression;
    ExpressionParser parser(expression, tokenize(source));
    expression.m_root = parser.parseOr();
    if (parser.position() < parser.tokenCount())
        throw PatternError(QStringLiteral("unexpected token near position %1").arg(parser.position() + 1));
    return expression;
}

bool Expression::evaluate(int index, const std::vector<bool> &present) const
{
    const Node &node = m_nodes[size_t(index)];
    switch (node.kind) {
    case Node::Term: return present[size_t(node.left)];
    case Node::Not: return !evaluate(node.left, present);
    case Node::And: return evaluate(node.left, present) && evaluate(node.right, present);
    case Node::Or: return evaluate(node.left, present) || evaluate(node.right, present);
    }
    return false;
}

std::vector<bool> Expression::positiveTerms() const
{
    std::vector<bool> result(m_terms.size(), false);
    collectPositive(m_root, false, result);
    return result;
}

void Expression::collectPositive(int index, bool negated, std::vector<bool> &out) const
{
    const Node &node = m_nodes[size_t(index)];
    switch (node.kind) {
    case Node::Term:
        if (!negated) out[size_t(node.left)] = true;
        break;
    case Node::Not:
        collectPositive(node.left, !negated, out);
        break;
    case Node::And:
    case Node::Or:
        collectPositive(node.left, negated, out);
        collectPositive(node.right, negated, out);
        break;
    }
}

QString Expression::describe(int index) const
{
    const Node &node = m_nodes[size_t(index)];
    switch (node.kind) {
    case Node::Term: return QStringLiteral("(term %1)").arg(node.left);
    case Node::Not: return QStringLiteral("(not %1)").arg(describe(node.left));
    case Node::And: return QStringLiteral("(and %1 %2)").arg(describe(node.left), describe(node.right));
    case Node::Or: return QStringLiteral("(or %1 %2)").arg(describe(node.left), describe(node.right));
    }
    return {};
}

} // namespace garimpo
