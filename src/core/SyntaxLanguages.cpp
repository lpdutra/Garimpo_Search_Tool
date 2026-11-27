#include "Syntax.h"

namespace garimpo {

namespace {

// MARK: Word lists

constexpr const char *kAblKeywords =
    "abstract accumulate add after alert-box all ambiguous and any append apply as asc ascending assign at "
    "attr-space auto-return available avail average before-hide begins bell between blank block-level break "
    "buffer buffer-compare buffer-copy by can-do can-find case case-sensitive catch centered choose class clear "
    "close colon column-label columns compile connect constructor contains control copy-lob count create current "
    "current-changed current-language current-value current-window data-relation data-source database dataset "
    "dataset-handle def default define delete delegate delimiter desc descending destructor dictionary disable "
    "disconnect display disp do down dynamic-function dynamic-new each editing else empty enable end entry enum "
    "eq error error-status escape event except exclusive exclusive-lock export extent field fields fill-in final "
    "finally find first first-of flags for form format forward frame from function ge get global go-on "
    "go-pending gt help hide if implements import in index inherits init initial input input-output insert "
    "interface into is iteration join keep-messages key label last last-of le leave left like locked lt matches "
    "message method modulo ne new next next-prompt no-box no-error no-labels no-lock no-prefetch no-undo no-wait "
    "non-serializable not num-entries of off on open or os-command os-copy os-delete os-getenv otherwise "
    "outer-join output override package-private package-protected page param parameter pause persistent "
    "preselect primary private procedure process prompt-for property protected public publish put query quit "
    "raw-transfer readkey record relation-fields release repeat reposition retry return return-value returns "
    "routine-level run save scroll search serializable set share-lock shared signature skip space static status "
    "stop stream stream-handle subscribe super table temp-table then this-object this-procedure throw to "
    "transaction trigger triggers undo unformatted union unique unsubscribe up update use-index using validate "
    "value var variable view view-as void wait-for when where while widget-handle with width work-table "
    "workfile xml-node-name "
    // Built-in functions and session handles.
    "absolute add-interval ascii asc buffer-handle can-query can-set caps chr compare day decimal-point "
    "dynamic-cast entry etime exp fill get-class get-message index integer-value interval iso-date lc "
    "left-trim length log lookup maximum minimum month mtime now num-results opsys os-error program-name "
    "propath quoter r-index random replace right-trim round session sqrt string substitute substring "
    "substr time timezone today trim truncate valid-handle valid-object weekday year";

constexpr const char *kAblTypes =
    "blob character char clob com-handle date datetime datetime-tz decimal dec handle int64 integer int "
    "logical log longchar memptr progress.lang.object raw recid rowid";

constexpr const char *kSqlKeywords =
    "add all alter and any as asc authid autonomous_transaction begin between body bulk by cascade case cast "
    "check close coalesce collect column comment commit connect constant constraint continue count create cross "
    "current cursor database decode declare default delete desc distinct drop else elsif end exception exec "
    "execute exists exit fetch first for forall foreign from full function go grant group having identity if "
    "immediate in index inner insert instr intersect into is join key last left level like limit loop matched "
    "max merge min minus modify natural next not nocopy nulls nvl nvl2 of offset on only open or order out "
    "outer over package partition pragma primary prior procedure raise rank record references rename replace "
    "return returning returns revoke right rollback round row row_number rownum rows rowtype savepoint schema "
    "select sequence set some start subtype sum synonym sysdate systimestamp table then to to_char to_date "
    "to_number top trigger trunc truncate type union unique update upper lower use using values view when "
    "where while with";

constexpr const char *kSqlTypes =
    "bigint binary_integer bit blob boolean char clob date datetime datetime2 decimal double float int integer "
    "interval long money nchar nclob number numeric nvarchar nvarchar2 pls_integer raw real serial smallint "
    "text time timestamp tinyint uuid varbinary varchar varchar2 xmltype";

constexpr const char *kCKeywords =
    "alignas alignof asm auto break case catch class co_await co_return co_yield concept const consteval "
    "constexpr constinit const_cast continue decltype default delete do dynamic_cast else enum explicit export "
    "extern final for friend goto if inline mutable namespace new noexcept operator override private protected "
    "public register reinterpret_cast requires return sizeof static static_assert static_cast struct switch "
    "template this thread_local throw try typedef typeid typename union using virtual volatile while";

constexpr const char *kCTypes =
    "bool char char8_t char16_t char32_t double float int int8_t int16_t int32_t int64_t intptr_t long ptrdiff_t "
    "short signed size_t ssize_t uint8_t uint16_t uint32_t uint64_t uintptr_t unsigned void wchar_t";

constexpr const char *kCSharpKeywords =
    "abstract add alias as async await base break by case catch checked class const continue default delegate "
    "descending do else enum equals event explicit extern file finally fixed for foreach from get global goto "
    "group if implicit in init interface internal into is join let lock nameof namespace new notnull on "
    "operator orderby out override params partial private protected public readonly record ref remove required "
    "return scoped sealed select set sizeof stackalloc static struct switch this throw try typeof unchecked "
    "unmanaged unsafe using value var virtual volatile when where while with yield";

constexpr const char *kCSharpTypes =
    "bool byte char decimal double dynamic float int long nint nuint object sbyte short string uint ulong "
    "ushort void";

constexpr const char *kJavaKeywords =
    "abstract assert break case catch class const continue default do else enum extends final finally for goto "
    "if implements import instanceof interface native new non-sealed package permits private protected public "
    "record return sealed static strictfp super switch synchronized this throw throws transient try var "
    "volatile while yield";

constexpr const char *kJavaTypes = "boolean byte char double float int long short void String Object";

constexpr const char *kJsKeywords =
    "as async await break case catch class const continue debugger default delete do else export extends "
    "finally for from function get if import in instanceof let new of return set static super switch this "
    "throw try typeof var void while with yield";

constexpr const char *kJsTypes =
    "Array BigInt Boolean Date Error Function JSON Map Math Number Object Promise RegExp Set String Symbol "
    "console document window";

constexpr const char *kTsKeywords =
    "abstract accessor as asserts async await break case catch class const continue debugger declare default "
    "delete do else enum export extends finally for from function get if implements import in infer instanceof "
    "interface is keyof let module namespace new of override private protected public readonly require return "
    "satisfies set static super switch this throw try type typeof unique var void while with yield";

constexpr const char *kTsTypes =
    "any bigint boolean never number object string symbol unknown void Array Date Error Map Promise Record "
    "Partial Readonly Set console";

constexpr const char *kGoKeywords =
    "break case chan const continue default defer else fallthrough for func go goto if import interface map "
    "package range return select struct switch type var append cap close copy delete len make new panic print "
    "println recover";

constexpr const char *kGoTypes =
    "any bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint "
    "uint8 uint16 uint32 uint64 uintptr";

constexpr const char *kRustKeywords =
    "as async await break const continue crate dyn else enum extern fn for if impl in let loop macro_rules "
    "match mod move mut pub ref return self Self static struct super trait type union unsafe use where while";

constexpr const char *kRustTypes =
    "bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize Box Option Result String Vec";

constexpr const char *kPhpKeywords =
    "abstract and array as break callable case catch class clone const continue declare default do echo else "
    "elseif empty enddeclare endfor endforeach endif endswitch endwhile enum extends final finally fn for "
    "foreach function global goto if implements include include_once instanceof insteadof interface isset list "
    "match namespace new or print private protected public readonly require require_once return static switch "
    "throw trait try unset use var while xor yield";

constexpr const char *kPythonKeywords =
    "and as assert async await break case class continue def del elif else except finally for from global if "
    "import in is lambda match nonlocal not or pass raise return try type while with yield";

constexpr const char *kPythonTypes = "bool bytes dict float int list object set str tuple self cls Exception";

constexpr const char *kRubyKeywords =
    "BEGIN END alias and attr_accessor attr_reader attr_writer begin break case class def defined? do else "
    "elsif end ensure for if in lambda module next not or private proc protected public puts raise redo "
    "require require_relative rescue retry return self super then undef unless until when while yield";

constexpr const char *kShellKeywords =
    "alias break case cd continue declare do done echo elif else esac eval exec exit export fi for function if "
    "in local printf read readonly return select set shift source test then time trap until unset while";

constexpr const char *kPowerShellKeywords =
    "begin break catch class continue data define do dynamicparam else elseif end enum exit filter finally for "
    "foreach from function hidden if in param process return static switch throw trap try until using var "
    "while workflow -and -as -band -bor -contains -eq -f -ge -gt -in -is -isnot -join -le -like -lt -match "
    "-ne -not -notcontains -notin -notlike -notmatch -or -replace -split -xor";

constexpr const char *kBatchKeywords =
    "call cd chdir cls copy defined del do echo else endlocal enabledelayedexpansion equ errorlevel exist exit "
    "for geq goto gtr if in leq lss md mkdir move neq not nul off on pause popd pushd rd ren rename rmdir set "
    "setlocal shift start title type";

constexpr const char *kVbKeywords =
    "addhandler addressof alias and andalso as byref byval call case catch cbool cbyte cchar cdate cdbl cdec "
    "cint class clng cobj const continue csng cstr ctype declare default delegate dim directcast do each else "
    "elseif end endif enum erase error event exit finally for friend function get gettype global gosub goto "
    "handles if implements imports in inherits interface is isnot let lib like loop me mod module mustinherit "
    "mustoverride mybase myclass namespace new next not notinheritable notoverridable of on operator option "
    "optional or orelse overloads overridable overrides paramarray partial private property protected public "
    "raiseevent readonly redim removehandler resume return select set shadows shared static step stop "
    "structure sub synclock then throw to try trycast typeof using wend when while with withevents writeonly "
    "xor";

constexpr const char *kVbTypes =
    "boolean byte char date decimal double integer long object sbyte short single string uinteger ulong "
    "ushort variant";

constexpr const char *kCssConstants =
    "absolute auto block bold both bottom center dashed dotted fixed flex grid hidden inherit initial inline "
    "inline-block italic left none normal nowrap pointer relative right solid sticky top transparent unset "
    "visible";

// MARK: Languages

constexpr SyntaxDefinition kDefinitions[] = {
    {.name = "ABL",
     .extensions = "p i w cls df",
     .caseSensitive = false,
     .keywords = kAblKeywords,
     .types = kAblTypes,
     .constants = "true false yes no",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .nestedComments = true,
     .quotes = "\"'",
     .multilineQuotes = "\"'",
     .escape = u'~',
     .ablDirectives = true,
     .wordChars = "-#$%",
     .symbolConstants = "?"},
    {.name = "SQL",
     .extensions = "sql pks pkb pls plsql prc fnc trg ddl dml",
     .caseSensitive = false,
     .keywords = kSqlKeywords,
     .types = kSqlTypes,
     .constants = "null true false",
     .lineComments = "--",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "'",
     .multilineQuotes = "'",
     .escape = 0,
     .variablePrefixes = "@:",
     .wordChars = "$#"},
    {.name = "C/C++",
     .extensions = "c h cc cpp cxx c++ hpp hxx hh h++ inl ipp tpp cppm ixx",
     .keywords = kCKeywords,
     .types = kCTypes,
     .constants = "true false nullptr NULL",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"'",
     .hashDirectives = true},
    {.name = "C#",
     .extensions = "cs csx",
     .keywords = kCSharpKeywords,
     .types = kCSharpTypes,
     .constants = "true false null",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"'",
     .hashDirectives = true},
    {.name = "Java",
     .extensions = "java jav",
     .keywords = kJavaKeywords,
     .types = kJavaTypes,
     .constants = "true false null",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"'",
     .variablePrefixes = "@"},
    {.name = "JavaScript",
     .extensions = "js mjs cjs jsx",
     .keywords = kJsKeywords,
     .types = kJsTypes,
     .constants = "true false null undefined NaN Infinity",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"'`",
     .multilineQuotes = "`"},
    {.name = "TypeScript",
     .extensions = "ts mts cts tsx",
     .keywords = kTsKeywords,
     .types = kTsTypes,
     .constants = "true false null undefined NaN Infinity",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"'`",
     .multilineQuotes = "`",
     .variablePrefixes = "@"},
    {.name = "Go",
     .extensions = "go",
     .keywords = kGoKeywords,
     .types = kGoTypes,
     .constants = "true false nil iota",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"'`",
     .multilineQuotes = "`"},
    {.name = "Rust",
     .extensions = "rs",
     .keywords = kRustKeywords,
     .types = kRustTypes,
     .constants = "true false None Some Ok Err",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .nestedComments = true,
     .quotes = "\"",
     .multilineQuotes = "\"",
     .hashDirectives = true},
    {.name = "PHP",
     .extensions = "php phtml php3 php4 php5 phps",
     .caseSensitive = false,
     .keywords = kPhpKeywords,
     .types = "int float bool string void mixed object iterable never self parent",
     .constants = "true false null",
     .lineComments = "// #",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"'",
     .multilineQuotes = "\"'",
     .variablePrefixes = "$"},
    {.name = "Python",
     .extensions = "py pyw pyi",
     .keywords = kPythonKeywords,
     .types = kPythonTypes,
     .constants = "True False None",
     .lineComments = "#",
     .quotes = "\"'",
     .tripleQuotes = true,
     .variablePrefixes = "@"},
    {.name = "Ruby",
     .extensions = "rb rbw rake gemspec ru",
     .keywords = kRubyKeywords,
     .constants = "true false nil",
     .lineComments = "#",
     .quotes = "\"'`",
     .multilineQuotes = "\"'`",
     .variablePrefixes = "$@:"},
    {.name = "Shell",
     .extensions = "sh bash zsh ksh command bashrc zshrc profile bash_profile zprofile",
     .keywords = kShellKeywords,
     .constants = "true false",
     .lineComments = "#",
     .quotes = "\"'`",
     .multilineQuotes = "\"'`",
     .variablePrefixes = "$"},
    {.name = "PowerShell",
     .extensions = "ps1 psm1 psd1",
     .caseSensitive = false,
     .keywords = kPowerShellKeywords,
     .lineComments = "#",
     .blockStart = "<#",
     .blockEnd = "#>",
     .quotes = "\"'",
     .multilineQuotes = "\"'",
     .escape = u'`',
     .variablePrefixes = "$"},
    {.name = "Batch",
     .extensions = "bat cmd btm",
     .caseSensitive = false,
     .keywords = kBatchKeywords,
     .startComments = "rem ::",
     .quotes = "\"",
     .escape = u'^',
     .variablePrefixes = "%!"},
    {.name = "Visual Basic",
     .extensions = "vb vbs vba bas frm ctl dsr",
     .caseSensitive = false,
     .keywords = kVbKeywords,
     .types = kVbTypes,
     .constants = "true false nothing null empty",
     .lineComments = "'",
     .startComments = "rem",
     .quotes = "\"",
     .escape = 0,
     .hashDirectives = true},
    {.name = "HTML",
     .extensions = "html htm xhtml shtml jsp asp aspx ascx cshtml razor vue",
     .scanner = SyntaxScanner::Markup},
    {.name = "XML",
     .extensions = "xml xsd xsl xslt svg plist csproj vbproj fsproj props targets config resx nuspec wsdl xaml "
                   "axaml storyboard xib",
     .scanner = SyntaxScanner::Markup},
    {.name = "CSS",
     .extensions = "css scss sass less",
     .constants = kCssConstants,
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"'",
     .variablePrefixes = "$@",
     .wordChars = "-",
     .keys = SyntaxKeys::LineStartColon},
    {.name = "JSON",
     .extensions = "json jsonc json5 geojson webmanifest code-workspace babelrc eslintrc",
     .constants = "true false null",
     .lineComments = "//",
     .blockStart = "/*",
     .blockEnd = "*/",
     .quotes = "\"",
     .keys = SyntaxKeys::QuotedBeforeColon},
    {.name = "YAML",
     .extensions = "yaml yml",
     .constants = "true false null yes no on off True False",
     .lineComments = "#",
     .quotes = "\"'",
     .variablePrefixes = "&*",
     .keys = SyntaxKeys::LineStartColon},
    {.name = "INI",
     .extensions = "ini cfg conf reg inf desktop editorconfig gitconfig url",
     .caseSensitive = false,
     .constants = "true false yes no on off",
     .startComments = "; #",
     .keys = SyntaxKeys::Assignment,
     .separators = "=:",
     .sections = true},
    {.name = "Properties",
     .extensions = "properties",
     .startComments = "# !",
     .keys = SyntaxKeys::Assignment,
     .separators = "=:"},
    {.name = "TOML",
     .extensions = "toml",
     .constants = "true false",
     .lineComments = "#",
     .quotes = "\"'",
     .tripleQuotes = true,
     .keys = SyntaxKeys::Assignment,
     .sections = true},
    {.name = "Markdown",
     .extensions = "md markdown mdown mkd mkdn",
     .scanner = SyntaxScanner::Markdown},
};

} // namespace

std::span<const SyntaxDefinition> syntaxDefinitions() { return kDefinitions; }

} // namespace garimpo
