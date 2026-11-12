#include "Documents.h"
#include "Export.h"
#include "Expression.h"
#include "Matcher.h"
#include "Replace.h"
#include "SearchEngine.h"
#include "Storage.h"
#include "TextCodec.h"
#include "TextLines.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <zlib.h>

using namespace garimpo;

namespace {

SearchQuery makeQuery(const QString &pattern, PatternMode mode = PatternMode::Literal)
{
    SearchQuery query;
    query.pattern = pattern;
    query.mode = mode;
    return query;
}

std::vector<int> lineNumbers(const MatchResult &result)
{
    std::vector<int> numbers;
    for (const LineMatch &match : result.matches) numbers.push_back(match.lineNumber);
    return numbers;
}

QString writeFile(const QTemporaryDir &dir, const QString &relative, const QByteArray &data)
{
    const QString path = dir.filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return path;
    file.write(data);
    return path;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

std::vector<FileResult> runSearch(const SearchQuery &query, SearchStats *stats = nullptr, ExtractionCache *cache = nullptr)
{
    std::vector<FileResult> files;
    const SearchStats result = SearchEngine::run(query, SearchLimits{}, nullptr, cache,
                                                 [&](std::vector<FileResult> &&batch, const SearchStats &) {
                                                     for (FileResult &file : batch) files.push_back(std::move(file));
                                                 });
    if (stats) *stats = result;
    std::sort(files.begin(), files.end(), [](const FileResult &a, const FileResult &b) { return a.path < b.path; });
    return files;
}

QStringList names(const std::vector<FileResult> &files)
{
    QStringList result;
    for (const FileResult &file : files) result.append(file.name());
    return result;
}

/// Builds a one-entry ZIP archive with a deflated member, as found in DOCX and ODT files.
QByteArray makeZip(const QByteArray &name, const QByteArray &content)
{
    QByteArray compressed(int(compressBound(uLong(content.size()))) + 16, Qt::Uninitialized);
    z_stream stream{};
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(content.data()));
    stream.avail_in = uInt(content.size());
    stream.next_out = reinterpret_cast<Bytef *>(compressed.data());
    stream.avail_out = uInt(compressed.size());
    deflate(&stream, Z_FINISH);
    compressed.resize(int(stream.total_out));
    deflateEnd(&stream);

    auto u16 = [](QByteArray &out, quint16 v) { out.append(char(v & 0xFF)); out.append(char(v >> 8)); };
    auto u32 = [&](QByteArray &out, quint32 v) { u16(out, quint16(v & 0xFFFF)); u16(out, quint16(v >> 16)); };
    const quint32 crc = quint32(crc32(0, reinterpret_cast<const Bytef *>(content.data()), uInt(content.size())));

    QByteArray zip;
    u32(zip, 0x04034b50); u16(zip, 20); u16(zip, 0); u16(zip, 8); u16(zip, 0); u16(zip, 0);
    u32(zip, crc); u32(zip, quint32(compressed.size())); u32(zip, quint32(content.size()));
    u16(zip, quint16(name.size())); u16(zip, 0);
    zip.append(name).append(compressed);
    const quint32 central = quint32(zip.size());
    u32(zip, 0x02014b50); u16(zip, 20); u16(zip, 20); u16(zip, 0); u16(zip, 8); u16(zip, 0); u16(zip, 0);
    u32(zip, crc); u32(zip, quint32(compressed.size())); u32(zip, quint32(content.size()));
    u16(zip, quint16(name.size())); u16(zip, 0); u16(zip, 0); u16(zip, 0); u16(zip, 0); u32(zip, 0); u32(zip, 0);
    zip.append(name);
    const quint32 centralSize = quint32(zip.size()) - central;
    u32(zip, 0x06054b50); u16(zip, 0); u16(zip, 0); u16(zip, 1); u16(zip, 1);
    u32(zip, centralSize); u32(zip, central); u16(zip, 0);
    return zip;
}

} // namespace

class CoreTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // Expressions

    void implicitAndBindsTighterThanOr()
    {
        const Expression expression = Expression::parse(QStringLiteral("alpha beta OR gamma"));
        QCOMPARE(expression.terms().size(), size_t(3));
        QCOMPARE(expression.describe(), QStringLiteral("(or (and (term 0) (term 1)) (term 2))"));
    }

    void notParenthesesPhrasesAndRegex()
    {
        const Expression expression = Expression::parse(QStringLiteral(R"(("exact phrase" | /fo+/) AND NOT -skip)"));
        QCOMPARE(expression.terms().at(0), (ExpressionTerm{QStringLiteral("exact phrase"), false}));
        QCOMPARE(expression.terms().at(1), (ExpressionTerm{QStringLiteral("fo+"), true}));
        QCOMPARE(expression.terms().at(2), (ExpressionTerm{QStringLiteral("skip"), false}));
        QCOMPARE(expression.describe(), QStringLiteral("(and (or (term 0) (term 1)) (not (not (term 2))))"));
    }

    void symbolOperatorsAndPositiveTerms()
    {
        const Expression expression = Expression::parse(QStringLiteral("a & !b"));
        QCOMPARE(expression.describe(), QStringLiteral("(and (term 0) (not (term 1)))"));
        QCOMPARE(expression.positiveTerms(), (std::vector<bool>{true, false}));
    }

    void expressionSyntaxErrors()
    {
        QVERIFY_THROWS_EXCEPTION(PatternError, Expression::parse(QStringLiteral("(alpha")));
        QVERIFY_THROWS_EXCEPTION(PatternError, Expression::parse(QStringLiteral("alpha OR")));
        QVERIFY_THROWS_EXCEPTION(PatternError, Expression::parse(QStringLiteral("\"open")));
    }

    // Matcher

    void literalIsCaseInsensitiveByDefault()
    {
        const Matcher matcher(makeQuery(QStringLiteral("hello")));
        const MatchResult result = matcher.match(QStringLiteral("Hello world\nnothing\nsay HELLO"), 100);
        QCOMPARE(lineNumbers(result), (std::vector<int>{1, 3}));
        QCOMPARE(result.matches[1].spans.first(), (Span{4, 5}));
    }

    void literalIgnoresRegexCharacters()
    {
        const Matcher matcher(makeQuery(QStringLiteral("a.b(c)")));
        QCOMPARE(lineNumbers(matcher.match(QStringLiteral("axb(c)\na.b(c)"), 10)), (std::vector<int>{2}));
    }

    void wholeWordAndCaseSensitive()
    {
        SearchQuery query = makeQuery(QStringLiteral("Find"));
        query.wholeWord = true;
        query.caseSensitive = true;
        const Matcher matcher(query);
        QCOMPARE(lineNumbers(matcher.match(QStringLiteral("Find it\nFinder\nfind\n(Find)"), 10)), (std::vector<int>{1, 4}));

        query.mode = PatternMode::Regex;
        QCOMPARE(lineNumbers(Matcher(query).match(QStringLiteral("Find it\nFinder\nfind\n(Find)"), 10)), (std::vector<int>{1, 4}));
    }

    void regexWithAccentsAndAnchors()
    {
        const Matcher matcher(makeQuery(QStringLiteral("situa[cç][aã]o"), PatternMode::Regex));
        QCOMPARE(matcher.match(QStringLiteral("Situação\nsituacao\nsituation"), 10).matches.size(), size_t(2));

        const Matcher anchored(makeQuery(QStringLiteral("^end$"), PatternMode::Regex));
        QCOMPARE(lineNumbers(anchored.match(QStringLiteral("x\r\nend\r\nthe end\r\nend"), 10)), (std::vector<int>{2, 4}));
    }

    void invalidRegexThrows()
    {
        QVERIFY_THROWS_EXCEPTION(PatternError, (void)Matcher(makeQuery(QStringLiteral("("), PatternMode::Regex)));
        QVERIFY_THROWS_EXCEPTION(PatternError, (void)Matcher(makeQuery(QString())));
    }

    void lineNumbersAcrossLineEndings()
    {
        const Matcher matcher(makeQuery(QStringLiteral("x")));
        QCOMPARE(lineNumbers(matcher.match(QStringLiteral("x\r\na\rx\n\nx"), 10)), (std::vector<int>{1, 3, 5}));
    }

    void multipleMatchesOnOneLine()
    {
        const Matcher matcher(makeQuery(QStringLiteral("ab")));
        const MatchResult result = matcher.match(QStringLiteral("ab ab\nab"), 10);
        QCOMPARE(result.matches.size(), size_t(2));
        QCOMPARE(result.matches[0].spans.size(), 2);
    }

    void expressionLineScope()
    {
        const Matcher matcher(makeQuery(QStringLiteral("customer AND NOT test"), PatternMode::Expression));
        const MatchResult result = matcher.match(QStringLiteral("customer id\ncustomer test\nother"), 10);
        QCOMPARE(lineNumbers(result), (std::vector<int>{1}));
        QCOMPARE(result.matches[0].spans.first(), (Span{0, 8}));
    }

    void expressionFileScope()
    {
        SearchQuery query = makeQuery(QStringLiteral("invoice AND total"), PatternMode::Expression);
        query.expressionScope = ExpressionScope::File;
        const Matcher matcher(query);
        QCOMPARE(lineNumbers(matcher.match(QStringLiteral("invoice header\nline\ngrand total"), 10)), (std::vector<int>{1, 3}));
        QVERIFY(!matcher.match(QStringLiteral("invoice only"), 10).fileMatches);

        query.pattern = QStringLiteral("NOT draft");
        const Matcher negative(query);
        QVERIFY(negative.match(QStringLiteral("final text"), 10).fileMatches);
        QVERIFY(!negative.match(QStringLiteral("a draft"), 10).fileMatches);
    }

    void limitTruncates()
    {
        const Matcher matcher(makeQuery(QStringLiteral("x")));
        const MatchResult result = matcher.match(QStringLiteral("x\nx\nx"), 2);
        QCOMPARE(result.matches.size(), size_t(2));
        QVERIFY(result.truncated);
    }

    void longLinesAreClippedAroundTheMatch()
    {
        const QString line = QString(5000, QLatin1Char('a')) + QStringLiteral("needle") + QString(5000, QLatin1Char('b'));
        const Matcher matcher(makeQuery(QStringLiteral("needle")));
        const LineMatch match = matcher.match(line, 1).matches.front();
        QCOMPARE(match.text.size(), Matcher::maxStoredLineLength);
        QCOMPARE(match.text.mid(match.spans.first().start, match.spans.first().length), QStringLiteral("needle"));
    }

    void fileNames()
    {
        const Matcher matcher(makeQuery(QStringLiteral("report")));
        QCOMPARE(matcher.matchName(QStringLiteral("Monthly-Report.pdf"))->first(), (Span{8, 6}));
        QVERIFY(!matcher.matchName(QStringLiteral("notes.txt")));
    }

    void mergeOverlappingSpans()
    {
        const Spans merged = Matcher::merge(Spans{Span{5, 3}, Span{0, 2}, Span{6, 4}});
        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged[0], (Span{0, 2}));
        QCOMPARE(merged[1], (Span{5, 5}));
    }

    // Text

    void decodesUtf8AndWindows1252()
    {
        const auto utf8 = TextCodec::decode(QStringLiteral("ação").toUtf8());
        QCOMPARE(utf8->text, QStringLiteral("ação"));
        QCOMPARE(utf8->encoding, TextEncoding::Utf8);

        const QByteArray legacy("pre\xE7o m\xE9" "dio \x80");
        const auto decoded = TextCodec::decode(legacy);
        QCOMPARE(decoded->text, QStringLiteral("preço médio €"));
        QCOMPARE(decoded->encoding, TextEncoding::Windows1252);
        QCOMPARE(*TextCodec::encode(decoded->text, decoded->encoding), legacy);
    }

    void keepsBomsAndRejectsBinary()
    {
        const QByteArray bom = QByteArray("\xEF\xBB\xBF") + "hi";
        const auto decoded = TextCodec::decode(bom);
        QCOMPARE(decoded->text, QStringLiteral("hi"));
        QCOMPARE(*TextCodec::encode(QStringLiteral("ok"), decoded->encoding), QByteArray("\xEF\xBB\xBF") + "ok");

        const QByteArray utf16 = QByteArray("\xFF\xFE", 2) + QByteArray("o\0l\0\xE1\0", 6);
        QCOMPARE(TextCodec::decode(utf16)->text, QStringLiteral("olá"));

        DecodeError error = DecodeError::Unreadable;
        QVERIFY(!TextCodec::decode(QByteArray("PK\0\1", 4), &error));
        QCOMPARE(error, DecodeError::Binary);
    }

    void splitsLinesKeepingTerminators()
    {
        const QString text = QStringLiteral("one\r\ntwo\nthree\rfour");
        const auto lines = TextLines::split(text);
        QCOMPARE(lines.size(), size_t(4));
        QCOMPARE(TextLines::content(text, lines[2]).toString(), QStringLiteral("three"));
        QCOMPARE(lines[0].terminatorLength, quint8(2));
        QCOMPARE(lines[3].terminatorLength, quint8(0));
        QCOMPARE(TextLines::split(QStringLiteral("a\nb\n")).size(), size_t(2));
        QCOMPARE(TextLines::split(QString()).size(), size_t(1));
        QCOMPARE(TextLines::lineAt(lines, 10), qsizetype(2));
    }

    // History

    void historyOrdersPinnedThenRecent()
    {
        History history;
        history.remember(QStringLiteral("/a"), HistoryField::Directories);
        QTest::qWait(2);
        history.remember(QStringLiteral("/b"), HistoryField::Directories);
        history.setPinned(true, QStringLiteral("/a"), HistoryField::Directories);
        QTest::qWait(2);
        history.remember(QStringLiteral("/c"), HistoryField::Directories);
        QCOMPARE(history.values(HistoryField::Directories), (QStringList{"/a", "/c", "/b"}));
    }

    void historyLimitKeepsPinned()
    {
        History history;
        history.setLimit(2);
        history.remember(QStringLiteral("pinned"), HistoryField::Patterns);
        history.setPinned(true, QStringLiteral("pinned"), HistoryField::Patterns);
        for (const char *value : {"1", "2", "3"}) {
            QTest::qWait(2);
            history.remember(QString::fromLatin1(value), HistoryField::Patterns);
        }
        QCOMPARE(history.values(HistoryField::Patterns), (QStringList{"pinned", "3"}));
        history.clear(HistoryField::Patterns);
        QCOMPARE(history.values(HistoryField::Patterns), QStringList{"pinned"});
    }

    void historyRoundTrip()
    {
        QTemporaryDir dir;
        History history;
        SearchQuery query = makeQuery(QStringLiteral("needle"));
        query.directories = QStringLiteral("/tmp");
        query.modifiedAfter = QDate(2026, 1, 2);
        history.remember(query);
        history.remember(QStringLiteral("thread"), HistoryField::Replacements);
        QVERIFY(history.save(dir.filePath(QStringLiteral("history.json"))));

        const History loaded = History::load(dir.filePath(QStringLiteral("history.json")));
        QCOMPARE(loaded.values(HistoryField::Directories), QStringList{"/tmp"});
        QCOMPARE(loaded.values(HistoryField::Replacements), QStringList{"thread"});
        QVERIFY(loaded.lastQuery == query);
    }

    // Search engine

    void masksExclusionsAndBinaries()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("src/main.cpp"), "let target = 1");
        writeFile(dir, QStringLiteral("src/readme.md"), "target in docs");
        writeFile(dir, QStringLiteral("node_modules/lib.cpp"), "target hidden in deps");
        writeFile(dir, QStringLiteral(".hidden/secret.cpp"), "target");
        writeFile(dir, QStringLiteral("src/image.cpp"), QByteArray("ta\0rg", 5));

        SearchQuery query = makeQuery(QStringLiteral("target"));
        query.directories = dir.path();
        query.includeMasks = QStringLiteral("*.cpp");
        SearchStats stats;
        const auto files = runSearch(query, &stats);
        QCOMPARE(names(files), QStringList{"main.cpp"});
        QCOMPARE(stats.skippedBinary, 1);
        QCOMPARE(stats.matchCount, 1);
    }

    void nonRecursiveAndHidden()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("top.txt"), "x");
        writeFile(dir, QStringLiteral("sub/deep.txt"), "x");
        writeFile(dir, QStringLiteral(".dot/inner.txt"), "x");

        SearchQuery query = makeQuery(QStringLiteral("x"));
        query.directories = dir.path();
        query.recursive = false;
        QCOMPARE(names(runSearch(query)), QStringList{"top.txt"});

        query.recursive = true;
        query.includeHidden = true;
        query.excludeDirectories.clear();
        QCOMPARE(runSearch(query).size(), size_t(3));
    }

    void emptyPatternListsFilesAndNamesOnly()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("a.log"), "");
        writeFile(dir, QStringLiteral("Invoice-2024.txt"), "nothing");
        writeFile(dir, QStringLiteral("other.txt"), "invoice inside");

        SearchQuery query;
        query.directories = dir.path();
        query.includeMasks = QStringLiteral("*.log");
        QCOMPARE(names(runSearch(query)), QStringList{"a.log"});

        query = makeQuery(QStringLiteral("invoice"));
        query.directories = dir.path();
        query.fileNamesOnly = true;
        const auto files = runSearch(query);
        QCOMPARE(names(files), QStringList{"Invoice-2024.txt"});
        QCOMPARE(files.front().nameSpans.first(), (Span{0, 7}));
    }

    void legacyEncodingAndSizeFilter()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("legacy.p"), QByteArray("/* cobran\xE7" "a */"));
        writeFile(dir, QStringLiteral("big.p"), QByteArray("cobran\xE7" "a") + QByteArray(4096, ' '));

        SearchQuery query = makeQuery(QStringLiteral("cobrança"));
        query.directories = dir.path();
        query.maxSizeKB = 2;
        const auto files = runSearch(query);
        QCOMPARE(names(files), QStringList{"legacy.p"});
        QCOMPARE(files.front().encoding, QStringLiteral("Windows-1252"));
    }

    void missingFolderThrows()
    {
        QVERIFY_THROWS_EXCEPTION(PatternError, runSearch(makeQuery(QStringLiteral("x"))));
    }

    void totalLimitStopsTheSearch()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 20; ++i) writeFile(dir, QStringLiteral("f%1.txt").arg(i), "x\nx\nx\n");
        SearchQuery query = makeQuery(QStringLiteral("x"));
        query.directories = dir.path();
        SearchLimits limits;
        limits.maxTotalMatches = 10;
        qint64 total = 0;
        const SearchStats stats = SearchEngine::run(query, limits, nullptr, nullptr, [&](std::vector<FileResult> &&files, const SearchStats &) {
            for (const FileResult &file : files) total += file.matchCount();
        });
        QVERIFY(stats.limitReached);
        QVERIFY(total <= 10);
    }

    // Documents

    void readsDocxOdtAndRtf()
    {
        const QByteArray wordXml = R"(<w:document xmlns:w="w"><w:body><w:p><w:r><w:t>Relatório de </w:t></w:r><w:r><w:t>cobrança</w:t></w:r></w:p><w:p><w:r><w:t>segunda</w:t></w:r></w:p></w:body></w:document>)";
        const QByteArray docx = makeZip("word/document.xml", wordXml);
        const auto xml = DocumentExtractor::readZipEntry(docx, QStringLiteral("word/document.xml"));
        QVERIFY(xml.has_value());
        QCOMPARE(DocumentExtractor::textFromWordXml(*xml), QStringLiteral("Relatório de cobrança\nsegunda\n"));

        const QByteArray odtXml = R"(<office:document-content xmlns:office="o" xmlns:text="t"><office:body><office:text><text:h>Title</text:h><text:p>a<text:s text:c="2"/>b</text:p></office:text></office:body></office:document-content>)";
        QCOMPARE(DocumentExtractor::textFromOpenDocumentXml(odtXml), QStringLiteral("Title\na  b\n"));

        const QByteArray rtf = "{\\rtf1\\ansi{\\fonttbl{\\f0 Helvetica;}}{\\*\\generator x;}\\f0 Pre\\'e7o \\u8364? total\\par Linha {\\b dois}}";
        QCOMPARE(DocumentExtractor::textFromRtf(rtf), QStringLiteral("Preço € total\nLinha dois"));
    }

    void searchesDocumentsAndCachesText()
    {
        QTemporaryDir dir;
        writeFile(dir, QStringLiteral("report.docx"),
                  makeZip("word/document.xml", R"(<w:document xmlns:w="w"><w:p><w:r><w:t>Quarterly revenue grew</w:t></w:r></w:p></w:document>)"));
        ExtractionCache cache(dir.filePath(QStringLiteral(".cache")));

        SearchQuery query = makeQuery(QStringLiteral("revenue"));
        query.directories = dir.path();
        query.searchDocuments = true;
        const auto files = runSearch(query, nullptr, &cache);
        QCOMPARE(files.size(), size_t(1));
        QCOMPARE(files.front().kind, FileKind::Document);
        QCOMPARE(files.front().matches.front().text, QStringLiteral("Quarterly revenue grew"));
        QCOMPARE(cache.removeStale().kept, 1);

        QFile::remove(dir.filePath(QStringLiteral("report.docx")));
        QCOMPARE(cache.removeStale().removed, 1);
    }

    // Replace

    void replaceKeepsEncodingAndLineEndingsThenUndo()
    {
        QTemporaryDir dir;
        const QByteArray original("pre\xE7o = 1\r\nmanter\r\npre\xE7o = 2\r\n");
        const QString file = writeFile(dir, QStringLiteral("prices.txt"), original);
        const QString backups = dir.filePath(QStringLiteral(".backups"));

        const ReplacePlan plan = Replacer::plan(makeQuery(QStringLiteral("preço")), QStringLiteral("valor"), {file});
        QCOMPARE(plan.selectedLineCount(), 2);

        const ReplaceOutcome outcome = Replacer::execute(plan, backups);
        QVERIFY(outcome.batch.has_value());
        QVERIFY(outcome.failures.empty());
        QCOMPARE(readFile(file), QByteArray("valor = 1\r\nmanter\r\nvalor = 2\r\n"));
        QCOMPARE(Replacer::lastBatch(backups)->id, outcome.batch->id);

        const UndoOutcome undo = Replacer::undo(*outcome.batch, false, backups);
        QCOMPARE(undo.restored, QStringList{file});
        QCOMPARE(readFile(file), original);
        QVERIFY(!Replacer::lastBatch(backups));
    }

    void regexGroupsAndLineSelection()
    {
        QTemporaryDir dir;
        const QString file = writeFile(dir, QStringLiteral("names.txt"), "Doe, John\nRoe, Jane\n");
        ReplacePlan plan = Replacer::plan(makeQuery(QStringLiteral("(\\w+), (\\w+)"), PatternMode::Regex), QStringLiteral("$2 $1"), {file});
        QCOMPARE(plan.files.front().lines[0].replaced, QStringLiteral("John Doe"));
        QCOMPARE(plan.files.front().lines[1].replaced, QStringLiteral("Jane Roe"));
        plan.files.front().lines[1].included = false;
        Replacer::execute(plan, dir.filePath(QStringLiteral(".backups")));
        QCOMPARE(readFile(file), QByteArray("John Doe\nRoe, Jane\n"));
    }

    void literalReplacementIsNotATemplate()
    {
        QTemporaryDir dir;
        const QString file = writeFile(dir, QStringLiteral("cost.txt"), "cost");
        const ReplacePlan plan = Replacer::plan(makeQuery(QStringLiteral("cost")), QStringLiteral("$1.00"), {file});
        QCOMPARE(plan.files.front().lines.front().replaced, QStringLiteral("$1.00"));
    }

    void undoKeepsFilesEditedAfterwards()
    {
        QTemporaryDir dir;
        const QString file = writeFile(dir, QStringLiteral("a.txt"), "old");
        const QString backups = dir.filePath(QStringLiteral(".backups"));
        const auto batch = Replacer::execute(Replacer::plan(makeQuery(QStringLiteral("old")), QStringLiteral("new"), {file}), backups).batch;
        QVERIFY(batch.has_value());
        writeFile(dir, QStringLiteral("a.txt"), "edited by hand");
        const UndoOutcome undo = Replacer::undo(*batch, false, backups);
        QCOMPARE(undo.modifiedSince, QStringList{file});
        QCOMPARE(readFile(file), QByteArray("edited by hand"));
    }

    void expressionModeCannotReplace()
    {
        QVERIFY_THROWS_EXCEPTION(PatternError,
                                 Replacer::plan(makeQuery(QStringLiteral("a AND b"), PatternMode::Expression), QStringLiteral("x"), {}));
    }

    // Export and documents on disk

    void exportFormats()
    {
        FileResult file;
        file.path = QStringLiteral("/p/a.txt");
        file.matches.push_back(LineMatch{3, QStringLiteral("say \"<hi>\""), Spans{Span{5, 4}}});
        ResultDocument document;
        document.title = QStringLiteral("t");
        document.files.push_back(file);

        QCOMPARE(Exporter::text(document), QStringLiteral("/p/a.txt:3:6: say \"<hi>\"\n"));
        QVERIFY(Exporter::csv(document).endsWith(QStringLiteral("\"/p/a.txt\",3,6,\"say \"\"<hi>\"\"\"\n")));
        QVERIFY(Exporter::html(document).contains(QStringLiteral("say &quot;<mark>&lt;hi&gt;</mark>&quot;")));
    }

    void resultDocumentRoundTrip()
    {
        QTemporaryDir dir;
        ResultDocument document;
        document.title = QStringLiteral("Saved");
        document.query = makeQuery(QStringLiteral("abc"));
        FileResult file;
        file.path = QStringLiteral("/x");
        file.matches.push_back(LineMatch{2, QStringLiteral("abc"), Spans{Span{0, 3}}});
        document.files.push_back(file);
        const QString path = dir.filePath(QStringLiteral("saved.garimpo"));
        QVERIFY(document.write(path));

        const auto loaded = ResultDocument::read(path);
        QCOMPARE(loaded->title, QStringLiteral("Saved"));
        QVERIFY(loaded->query == document.query);
        QCOMPARE(loaded->files.front().matches.front().spans.first(), (Span{0, 3}));
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
