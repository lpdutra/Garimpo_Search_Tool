#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

#include <span>
#include <vector>

namespace garimpo {

enum class SyntaxToken : quint8 {
    Keyword,
    Type,
    Constant,
    Number,
    String,
    Comment,
    Preprocessor,
    Tag,
    Attribute,
    Variable,
    Heading,
    Emphasis,
    Code,
};
inline constexpr int kSyntaxTokenCount = int(SyntaxToken::Code) + 1;

/// A colored range inside one line, in UTF-16 units.
struct SyntaxSpan {
    qint32 start = 0;
    qint32 length = 0;
    SyntaxToken token = SyntaxToken::Keyword;
    bool operator==(const SyntaxSpan &) const = default;
};

enum class SyntaxScanner : quint8 { Code, Markup, Markdown };

enum class SyntaxKeys : quint8 {
    None,
    /// A quoted string followed by ':' is a key (JSON).
    QuotedBeforeColon,
    /// Also a bare `key:` at the start of a line (YAML, CSS).
    LineStartColon,
    /// `key = value` lines (INI, properties, TOML).
    Assignment,
};

/// A language described as data. Word lists are separated by spaces.
struct SyntaxDefinition {
    const char *name;
    /// File name extensions, lower case, without the dot.
    const char *extensions;
    SyntaxScanner scanner = SyntaxScanner::Code;
    bool caseSensitive = true;
    const char *keywords = "";
    const char *types = "";
    const char *constants = "";
    /// Comments that run to the end of the line from anywhere.
    const char *lineComments = "";
    /// Comments recognized only as the first text of a line.
    const char *startComments = "";
    const char *blockStart = "";
    const char *blockEnd = "";
    bool nestedComments = false;
    /// Characters that open and close strings.
    const char *quotes = "";
    /// The quotes whose strings may continue on the next line.
    const char *multilineQuotes = "";
    /// Escapes the next character inside strings; a doubled quote always stands for itself.
    char16_t escape = u'\\';
    bool tripleQuotes = false;
    /// Lines starting with '#' are directives.
    bool hashDirectives = false;
    /// `&DIRECTIVE` words and `{include}` references.
    bool ablDirectives = false;
    /// Characters that start a variable name, such as '$' or '@'.
    const char *variablePrefixes = "";
    /// Characters allowed inside words besides letters, digits and '_'.
    const char *wordChars = "";
    /// Single characters shown as constants, such as ABL's unknown value '?'.
    const char *symbolConstants = "";
    SyntaxKeys keys = SyntaxKeys::None;
    /// Characters that end a key in `SyntaxKeys::Assignment` lines.
    const char *separators = "=";
    /// `[section]` lines.
    bool sections = false;
};

/// Built-in languages.
std::span<const SyntaxDefinition> syntaxDefinitions();

class SyntaxLanguage;

namespace Syntax {

/// The language for a file name extension, or null for plain text.
const SyntaxLanguage *forPath(const QString &path);
const SyntaxLanguage *named(const QString &name);
QString name(const SyntaxLanguage &language);
QStringList names();

/// Colors one line that starts in `state` (0 at the top of a file) and returns the state the next line
/// starts in. `spans` may be null to only follow the state.
int highlightLine(const SyntaxLanguage &language, QStringView line, int state, std::vector<SyntaxSpan> *spans);

/// The state each line of `text` starts in, splitting lines the way QTextDocument splits paragraphs.
std::vector<int> lineStates(const SyntaxLanguage &language, QStringView text);

} // namespace Syntax

} // namespace garimpo
