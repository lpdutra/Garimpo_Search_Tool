#pragma once

#include <QString>

#include <stdexcept>
#include <vector>

namespace garimpo {

class PatternError : public std::runtime_error {
public:
    explicit PatternError(const QString &message) : std::runtime_error(message.toStdString()), m_message(message) {}
    QString message() const { return m_message; }

private:
    QString m_message;
};

struct ExpressionTerm {
    QString text;
    bool isRegex = false;
    bool operator==(const ExpressionTerm &) const = default;
};

/// Boolean search expression.
///
///     expr    := or
///     or      := and (("OR" | "|") and)*
///     and     := unary (("AND" | "&")? unary)*      adjacent terms are ANDed
///     unary   := ("NOT" | "!" | "-") unary | primary
///     primary := "(" expr ")" | "quoted phrase" | /regex/ | word
class Expression {
public:
    struct Node {
        enum Kind { Term, Not, And, Or } kind;
        int left = -1;   // term index for Term, operand for Not
        int right = -1;
    };

    static Expression parse(const QString &source);

    const std::vector<ExpressionTerm> &terms() const { return m_terms; }
    const std::vector<Node> &nodes() const { return m_nodes; }
    int root() const { return m_root; }

    /// `present[i]` tells whether term i was found.
    bool evaluate(const std::vector<bool> &present) const { return evaluate(m_root, present); }
    /// Terms that are not negated; only these are highlighted.
    std::vector<bool> positiveTerms() const;
    /// Compact form such as `(and (term 0) (not (term 1)))`, used by tests.
    QString describe() const { return describe(m_root); }

private:
    bool evaluate(int node, const std::vector<bool> &present) const;
    void collectPositive(int node, bool negated, std::vector<bool> &out) const;
    QString describe(int node) const;

    std::vector<ExpressionTerm> m_terms;
    std::vector<Node> m_nodes;
    int m_root = -1;

    friend class ExpressionParser;
};

} // namespace garimpo
