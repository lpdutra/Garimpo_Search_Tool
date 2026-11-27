#include "Syntax.h"

#include <QHash>
#include <QSet>

#include <memory>

namespace garimpo {

class SyntaxLanguage {
public:
    explicit SyntaxLanguage(const SyntaxDefinition &definition);

    const SyntaxDefinition &def;
    QString name;
    Qt::CaseSensitivity sensitivity;
    QSet<QString> keywords;
    QSet<QString> types;
    QSet<QString> constants;
    QStringList lineComments;
    QStringList startComments;
    QString blockStart;
    QString blockEnd;
    QString quotes;
    QString multilineQuotes;
    QString variablePrefixes;
    QString wordChars;
    QString symbolConstants;
    QString separators;
    QChar escape;
    /// Some keywords start with '-', such as PowerShell's -eq.
    bool dashKeywords = false;
};

namespace {

QStringList words(const char *list) { return QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts); }

QSet<QString> wordSet(const char *list, bool caseSensitive)
{
    QSet<QString> set;
    for (const QString &word : words(list)) set.insert(caseSensitive ? word : word.toLower());
    return set;
}

} // namespace

SyntaxLanguage::SyntaxLanguage(const SyntaxDefinition &definition)
    : def(definition),
      name(QString::fromLatin1(definition.name)),
      sensitivity(definition.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive),
      keywords(wordSet(definition.keywords, definition.caseSensitive)),
      types(wordSet(definition.types, definition.caseSensitive)),
      constants(wordSet(definition.constants, definition.caseSensitive)),
      lineComments(words(definition.lineComments)),
      startComments(words(definition.startComments)),
      blockStart(QString::fromLatin1(definition.blockStart)),
      blockEnd(QString::fromLatin1(definition.blockEnd)),
      quotes(QString::fromLatin1(definition.quotes)),
      multilineQuotes(QString::fromLatin1(definition.multilineQuotes)),
      variablePrefixes(QString::fromLatin1(definition.variablePrefixes)),
      wordChars(QString::fromLatin1(definition.wordChars)),
      symbolConstants(QString::fromLatin1(definition.symbolConstants)),
      separators(QString::fromLatin1(definition.separators)),
      escape(definition.escape)
{
    for (const QString &word : keywords) {
        if (word.startsWith(QLatin1Char('-'))) {
            dashKeywords = true;
            break;
        }
    }
}

namespace {

struct Registry {
    std::vector<std::unique_ptr<SyntaxLanguage>> languages;
    QHash<QString, const SyntaxLanguage *> byExtension;
    QHash<QString, const SyntaxLanguage *> byName;

    Registry()
    {
        for (const SyntaxDefinition &definition : syntaxDefinitions()) {
            languages.push_back(std::make_unique<SyntaxLanguage>(definition));
            const SyntaxLanguage *language = languages.back().get();
            byName.insert(language->name.toLower(), language);
            for (const QString &extension : words(definition.extensions)) byExtension.insert(extension, language);
        }
    }
};

const Registry &registry()
{
    static const Registry instance;
    return instance;
}

// MARK: State

// The low four bits hold what continues on the next line; the rest holds its detail
// (comment depth, or the quote that is still open).
enum Mode : int {
    Normal = 0,
    InComment = 1,
    InString = 2,
    InTag = 3,
    InTagString = 4,
    InMarkupComment = 5,
    InCData = 6,
    InFence = 7,
};

int makeState(Mode mode, int detail = 0) { return mode | (detail << 4); }
Mode modeOf(int state) { return Mode(state & 0xF); }
int detailOf(int state) { return state >> 4; }
constexpr int kTripleFlag = 1 << 16;

struct Output {
    std::vector<SyntaxSpan> *spans;
    void add(int start, int end, SyntaxToken token) const
    {
        if (spans && end > start) spans->push_back({start, end - start, token});
    }
};

bool startsAt(QStringView text, int index, QStringView token, Qt::CaseSensitivity sensitivity = Qt::CaseSensitive)
{
    return !token.isEmpty() && text.size() - index >= token.size()
        && text.mid(index, token.size()).compare(token, sensitivity) == 0;
}

int skipSpaces(QStringView text, int index)
{
    while (index < text.size() && text[index].isSpace()) ++index;
    return index;
}

bool isWordStart(QChar ch) { return ch.isLetter() || ch == u'_'; }

bool isWordChar(const SyntaxLanguage &language, QChar ch)
{
    return ch.isLetterOrNumber() || ch == u'_' || language.wordChars.contains(ch);
}

int wordEnd(const SyntaxLanguage &language, QStringView text, int index)
{
    while (index < text.size() && isWordChar(language, text[index])) ++index;
    return index;
}

// MARK: Code

/// Skips a block comment body; `depth` counts the comments still open.
int skipComment(const SyntaxLanguage &language, QStringView text, int index, int &depth)
{
    const int size = int(text.size());
    while (index < size) {
        if (startsAt(text, index, language.blockEnd)) {
            index += int(language.blockEnd.size());
            if (--depth == 0) return index;
        } else if (language.def.nestedComments && startsAt(text, index, language.blockStart)) {
            index += int(language.blockStart.size());
            ++depth;
        } else {
            ++index;
        }
    }
    return index;
}

/// Skips a string body after its opening quote; returns the index after the closing quote.
int skipString(const SyntaxLanguage &language, QStringView text, int index, QChar quote, bool triple, bool &closed)
{
    const int size = int(text.size());
    while (index < size) {
        const QChar ch = text[index];
        if (!language.escape.isNull() && ch == language.escape && ch != quote) {
            index += 2;
            continue;
        }
        if (ch == quote) {
            if (triple) {
                if (index + 2 < size && text[index + 1] == quote && text[index + 2] == quote) {
                    closed = true;
                    return index + 3;
                }
                ++index;
                continue;
            }
            if (index + 1 < size && text[index + 1] == quote) {
                index += 2;
                continue;
            }
            closed = true;
            return index + 1;
        }
        ++index;
    }
    closed = false;
    return size;
}

/// End of a variable that starts with a prefix character at `index`, or `index` when there is none.
int variableEnd(const SyntaxLanguage &language, QStringView text, int index)
{
    const int size = int(text.size());
    const QChar prefix = text[index];
    int next = index + 1;
    if (next >= size) return index;
    const QChar ch = text[next];
    if (prefix == u'%' || prefix == u'!') {
        // Batch: %%i, %1, %~dp0, %name% and !name!.
        if (prefix == u'%' && ch == u'%') return wordEnd(language, text, next + 1);
        if (prefix == u'%' && (ch.isDigit() || ch == u'~')) {
            while (next < size && (text[next].isLetterOrNumber() || text[next] == u'~')) ++next;
            return next;
        }
        const qsizetype close = text.indexOf(prefix, next);
        if (close <= next) return index;
        for (qsizetype i = next; i < close; ++i)
            if (!isWordChar(language, text[i]) && text[i] != u':' && text[i] != u'~' && text[i] != u'=') return index;
        return int(close) + 1;
    }
    if (ch == u'{') {
        const qsizetype close = text.indexOf(u'}', next);
        return close < 0 ? size : int(close) + 1;
    }
    if (prefix == u'$' && QStringView(u"#?@*!$-").contains(ch)) return next + 1;
    if (!isWordStart(ch) && !(prefix == u'$' && ch.isDigit())) return index;
    return wordEnd(language, text, next);
}

SyntaxToken classify(const SyntaxLanguage &language, QStringView word, bool &found)
{
    const QString key = language.def.caseSensitive ? word.toString() : word.toString().toLower();
    found = true;
    if (language.keywords.contains(key)) return SyntaxToken::Keyword;
    if (language.types.contains(key)) return SyntaxToken::Type;
    if (language.constants.contains(key)) return SyntaxToken::Constant;
    found = false;
    return SyntaxToken::Keyword;
}

int scanCode(const SyntaxLanguage &language, QStringView text, int state, Output out)
{
    const int size = int(text.size());
    const SyntaxDefinition &def = language.def;
    int index = 0;

    // Finish what the previous line left open.
    if (modeOf(state) == InComment) {
        int depth = detailOf(state);
        index = skipComment(language, text, 0, depth);
        out.add(0, index, SyntaxToken::Comment);
        if (depth > 0) return makeState(InComment, depth);
    } else if (modeOf(state) == InString) {
        const QChar quote(char16_t(detailOf(state) & 0xFFFF));
        const bool triple = detailOf(state) & kTripleFlag;
        bool closed = false;
        index = skipString(language, text, 0, quote, triple, closed);
        out.add(0, index, SyntaxToken::String);
        if (!closed) return state;
    }

    const int first = skipSpaces(text, index);
    if (first >= size) return Normal;

    if (index == 0) {
        for (const QString &comment : language.startComments) {
            const bool isWord = comment.back().isLetter();
            if (startsAt(text, first, comment, language.sensitivity)
                && (!isWord || first + comment.size() >= size || !isWordChar(language, text[first + comment.size()]))) {
                out.add(first, size, SyntaxToken::Comment);
                return Normal;
            }
        }
        if (def.hashDirectives && text[first] == u'#') {
            out.add(first, size, SyntaxToken::Preprocessor);
            return Normal;
        }
        if (def.sections && text[first] == u'[') {
            const qsizetype close = text.lastIndexOf(u']');
            index = close > first ? int(close) + 1 : size;
            out.add(first, index, SyntaxToken::Type);
        } else if (def.keys == SyntaxKeys::Assignment) {
            int end = first;
            while (end < size && !language.separators.contains(text[end]) && !language.quotes.contains(text[end])) ++end;
            if (end < size && language.separators.contains(text[end])) {
                int keyEnd = end;
                while (keyEnd > first && text[keyEnd - 1].isSpace()) --keyEnd;
                out.add(first, keyEnd, SyntaxToken::Attribute);
                index = end + 1;
            }
        } else if (def.keys == SyntaxKeys::LineStartColon) {
            int key = first;
            if (text[key] == u'-' && key + 1 < size && text[key + 1] == u' ') key = skipSpaces(text, key + 1);
            if (key < size && !language.quotes.contains(text[key]) && text[key] != u'#') {
                for (int i = key; i < size; ++i) {
                    if (text[i] == u'#' && i > key && text[i - 1].isSpace()) break;
                    if (text[i] == u':' && (i + 1 == size || text[i + 1].isSpace())) {
                        out.add(key, i, SyntaxToken::Attribute);
                        index = i + 1;
                        break;
                    }
                }
            }
        }
    }
    index = qMax(index, first);

    while (index < size) {
        const QChar ch = text[index];
        if (ch.isSpace()) {
            ++index;
            continue;
        }

        bool lineComment = false;
        for (const QString &comment : language.lineComments) {
            if (startsAt(text, index, comment)) {
                lineComment = true;
                break;
            }
        }
        if (lineComment) {
            out.add(index, size, SyntaxToken::Comment);
            return Normal;
        }

        if (startsAt(text, index, language.blockStart)) {
            int depth = 1;
            const int end = skipComment(language, text, index + int(language.blockStart.size()), depth);
            out.add(index, end, SyntaxToken::Comment);
            if (depth > 0) return makeState(InComment, depth);
            index = end;
            continue;
        }

        if (language.quotes.contains(ch)) {
            const bool triple = def.tripleQuotes && index + 2 < size && text[index + 1] == ch && text[index + 2] == ch;
            bool closed = false;
            const int end = skipString(language, text, index + (triple ? 3 : 1), ch, triple, closed);
            SyntaxToken token = SyntaxToken::String;
            if (closed && def.keys != SyntaxKeys::None && def.keys != SyntaxKeys::Assignment) {
                const int next = skipSpaces(text, end);
                if (next < size && text[next] == u':') token = SyntaxToken::Attribute;
            }
            out.add(index, end, token);
            if (!closed) {
                if (triple || language.multilineQuotes.contains(ch))
                    return makeState(InString, ch.unicode() | (triple ? kTripleFlag : 0));
                return Normal;
            }
            index = end;
            continue;
        }

        if (def.ablDirectives) {
            if (ch == u'{') {
                int depth = 0;
                int end = index;
                for (; end < size; ++end) {
                    if (text[end] == u'{') ++depth;
                    else if (text[end] == u'}' && --depth == 0) {
                        ++end;
                        break;
                    }
                }
                out.add(index, end, SyntaxToken::Preprocessor);
                index = end;
                continue;
            }
            if (ch == u'&' && index + 1 < size && text[index + 1].isLetter()) {
                const int end = wordEnd(language, text, index + 1);
                out.add(index, end, SyntaxToken::Preprocessor);
                index = end;
                continue;
            }
        }

        if (ch.isDigit() || (ch == u'.' && index + 1 < size && text[index + 1].isDigit())) {
            int end = index + 1;
            while (end < size && (text[end].isLetterOrNumber() || text[end] == u'.' || text[end] == u'_')) ++end;
            // "1." ending an ABL statement is not part of the number.
            if (end > index + 1 && text[end - 1] == u'.') --end;
            out.add(index, end, SyntaxToken::Number);
            index = end;
            continue;
        }

        if (language.variablePrefixes.contains(ch)) {
            const int end = variableEnd(language, text, index);
            if (end > index + 1) {
                out.add(index, end, SyntaxToken::Variable);
                index = end;
                continue;
            }
        }

        if (ch == u'-' && language.dashKeywords && index + 1 < size && text[index + 1].isLetter()) {
            const int end = wordEnd(language, text, index + 1);
            bool found = false;
            const SyntaxToken token = classify(language, text.mid(index, end - index), found);
            if (found) {
                out.add(index, end, token);
                index = end;
                continue;
            }
        }

        if (isWordStart(ch)) {
            int end = wordEnd(language, text, index + 1);
            // ABL words may hold '-', but not end with it.
            while (end > index + 1 && text[end - 1] == u'-') --end;
            // Members after a dot (customer.name, obj.class) are names, not keywords.
            const bool member = index > 1 && text[index - 1] == u'.' && isWordChar(language, text[index - 2]);
            if (out.spans && !member) {
                bool found = false;
                const SyntaxToken token = classify(language, text.mid(index, end - index), found);
                if (found) out.add(index, end, token);
            }
            index = end;
            continue;
        }

        if (language.symbolConstants.contains(ch)) out.add(index, index + 1, SyntaxToken::Constant);
        ++index;
    }
    return Normal;
}

// MARK: Markup

int scanMarkup(QStringView text, int state, Output out)
{
    const int size = int(text.size());
    Mode mode = modeOf(state);
    QChar quote(char16_t(detailOf(state)));
    int index = 0;

    auto closeSpan = [&](QStringView terminator, SyntaxToken token) {
        const qsizetype found = text.indexOf(terminator, index);
        const int end = found < 0 ? size : int(found + terminator.size());
        out.add(index, end, token);
        index = end;
        if (found >= 0) mode = Normal;
    };

    while (index < size) {
        switch (mode) {
        case InMarkupComment:
            closeSpan(u"-->", SyntaxToken::Comment);
            break;
        case InCData:
            closeSpan(u"]]>", SyntaxToken::Code);
            break;
        case InTagString: {
            const qsizetype found = text.indexOf(quote, index);
            const int end = found < 0 ? size : int(found) + 1;
            out.add(index, end, SyntaxToken::String);
            index = end;
            if (found >= 0) mode = InTag;
            break;
        }
        case InTag: {
            index = skipSpaces(text, index);
            if (index >= size) break;
            const QChar ch = text[index];
            if (ch == u'>') {
                out.add(index, index + 1, SyntaxToken::Tag);
                ++index;
                mode = Normal;
            } else if ((ch == u'/' || ch == u'?') && index + 1 < size && text[index + 1] == u'>') {
                out.add(index, index + 2, SyntaxToken::Tag);
                index += 2;
                mode = Normal;
            } else if (ch == u'"' || ch == u'\'') {
                quote = ch;
                const qsizetype found = text.indexOf(quote, index + 1);
                const int end = found < 0 ? size : int(found) + 1;
                out.add(index, end, SyntaxToken::String);
                index = end;
                if (found < 0) mode = InTagString;
            } else if (ch == u'=') {
                ++index;
            } else {
                int end = index;
                while (end < size && !text[end].isSpace() && text[end] != u'=' && text[end] != u'>' && text[end] != u'"'
                       && text[end] != u'\'' && !(text[end] == u'/' && end + 1 < size && text[end + 1] == u'>'))
                    ++end;
                if (end == index) ++end;
                out.add(index, end, SyntaxToken::Attribute);
                index = end;
            }
            break;
        }
        default: {
            const QChar ch = text[index];
            if (ch == u'&') {
                int end = index + 1;
                while (end < size && (text[end].isLetterOrNumber() || text[end] == u'#')) ++end;
                if (end < size && text[end] == u';' && end > index + 1) {
                    out.add(index, end + 1, SyntaxToken::Constant);
                    index = end + 1;
                } else {
                    ++index;
                }
            } else if (ch != u'<') {
                ++index;
            } else if (startsAt(text, index, u"<!--")) {
                mode = InMarkupComment;
            } else if (startsAt(text, index, u"<![CDATA[")) {
                out.add(index, index + 9, SyntaxToken::Tag);
                index += 9;
                mode = InCData;
            } else if (index + 1 < size && text[index + 1] == u'!') {
                closeSpan(u">", SyntaxToken::Preprocessor);
            } else if (index + 1 < size && (text[index + 1].isLetter() || text[index + 1] == u'/' || text[index + 1] == u'?')) {
                int end = index + 2;
                while (end < size && (text[end].isLetterOrNumber() || QStringView(u":-_.").contains(text[end]))) ++end;
                out.add(index, end, text[index + 1] == u'?' ? SyntaxToken::Preprocessor : SyntaxToken::Tag);
                index = end;
                mode = InTag;
            } else {
                ++index;
            }
            break;
        }
        }
    }
    return mode == InTagString ? makeState(InTagString, quote.unicode()) : makeState(mode);
}

// MARK: Markdown

int scanMarkdown(QStringView text, int state, Output out)
{
    const int size = int(text.size());
    const int first = skipSpaces(text, 0);
    const bool fence = startsAt(text, first, u"```") || startsAt(text, first, u"~~~");
    if (modeOf(state) == InFence) {
        out.add(0, size, SyntaxToken::Code);
        return fence ? Normal : state;
    }
    if (fence) {
        out.add(0, size, SyntaxToken::Code);
        return makeState(InFence);
    }
    if (first >= size) return Normal;

    if (text[first] == u'#') {
        int level = first;
        while (level < size && text[level] == u'#') ++level;
        if (level - first <= 6 && (level == size || text[level] == u' ')) {
            out.add(first, size, SyntaxToken::Heading);
            return Normal;
        }
    }
    if (text[first] == u'>') {
        out.add(first, size, SyntaxToken::Comment);
        return Normal;
    }

    int index = first;
    if ((text[first] == u'-' || text[first] == u'*' || text[first] == u'+') && first + 1 < size && text[first + 1] == u' ') {
        out.add(first, first + 1, SyntaxToken::Keyword);
        index = first + 2;
    } else if (text[first].isDigit()) {
        int end = first;
        while (end < size && text[end].isDigit()) ++end;
        if (end + 1 < size && (text[end] == u'.' || text[end] == u')') && text[end + 1] == u' ') {
            out.add(first, end + 1, SyntaxToken::Keyword);
            index = end + 2;
        }
    }

    while (index < size) {
        const QChar ch = text[index];
        if (ch == u'`') {
            const qsizetype close = text.indexOf(u'`', index + 1);
            if (close > index) {
                out.add(index, int(close) + 1, SyntaxToken::Code);
                index = int(close) + 1;
                continue;
            }
        } else if ((ch == u'*' || ch == u'_') && index + 1 < size && text[index + 1] == ch) {
            const QChar marker[2] = {ch, ch};
            const qsizetype close = text.indexOf(QStringView(marker, 2), index + 2);
            if (close > index + 2) {
                out.add(index, int(close) + 2, SyntaxToken::Emphasis);
                index = int(close) + 2;
                continue;
            }
        } else if (ch == u'[') {
            const qsizetype middle = text.indexOf(u"](", index + 1);
            const qsizetype close = middle < 0 ? -1 : text.indexOf(u')', middle + 2);
            if (close > 0) {
                out.add(index, int(middle) + 1, SyntaxToken::Attribute);
                out.add(int(middle) + 1, int(close) + 1, SyntaxToken::Constant);
                index = int(close) + 1;
                continue;
            }
        }
        ++index;
    }
    return Normal;
}

} // namespace

namespace Syntax {

const SyntaxLanguage *forPath(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot <= slash) return nullptr;
    return registry().byExtension.value(path.mid(dot + 1).toLower());
}

const SyntaxLanguage *named(const QString &name) { return registry().byName.value(name.toLower()); }

QString name(const SyntaxLanguage &language) { return language.name; }

QStringList names()
{
    QStringList list;
    for (const auto &language : registry().languages) list.append(language->name);
    return list;
}

int highlightLine(const SyntaxLanguage &language, QStringView line, int state, std::vector<SyntaxSpan> *spans)
{
    const Output out{spans};
    switch (language.def.scanner) {
    case SyntaxScanner::Markup: return scanMarkup(line, state, out);
    case SyntaxScanner::Markdown: return scanMarkdown(line, state, out);
    case SyntaxScanner::Code: break;
    }
    return scanCode(language, line, state, out);
}

std::vector<int> lineStates(const SyntaxLanguage &language, QStringView text)
{
    std::vector<int> states;
    int state = Normal;
    qsizetype start = 0;
    const qsizetype size = text.size();
    for (qsizetype i = 0; i <= size; ++i) {
        const bool end = i == size;
        const QChar ch = end ? QChar() : text[i];
        if (!end && ch != u'\n' && ch != u'\r' && ch != QChar::ParagraphSeparator) continue;
        states.push_back(state);
        state = highlightLine(language, text.mid(start, i - start), state, nullptr);
        if (ch == u'\r' && i + 1 < size && text[i + 1] == u'\n') ++i;
        start = i + 1;
    }
    return states;
}

} // namespace Syntax

} // namespace garimpo
