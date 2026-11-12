#include "Export.h"
#include "Replace.h"
#include "SearchEngine.h"
#include "Storage.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QLocale>

#include <algorithm>
#include <cstdio>

using namespace garimpo;

namespace {

const char *kUsage = R"(usage: garimpo <command> [options]

commands:
  search  <folder>... [-p PATTERN] [options]   Search file contents (lists files when -p is omitted)
  replace <folder>... -p PATTERN -r TEXT       Replace text, backing up every changed file
  undo                                         Restore the files changed by the last replace
  cache   clean|clear|size                     Manage the document text cache

options:
  -p, --pattern TEXT        Pattern to search for
  -r, --replace TEXT        Replacement text (regex mode accepts $1, $2, ${name})
  -e, --regex               Treat the pattern as a regular expression
  -x, --expr                Treat the pattern as a boolean expression (AND, OR, NOT, "phrase", /regex/)
      --file-scope          Evaluate expressions against the whole file instead of each line
  -c, --case-sensitive      Match case
  -w, --word                Match whole words
  -m, --mask GLOBS          Include masks separated by ';' (default '*')
      --exclude GLOBS       File masks to skip
      --exclude-dir GLOBS   Folder names to skip (replaces the default list)
      --no-recurse          Do not search subfolders
      --hidden              Include hidden files and folders
      --names               Match the pattern against file names only
      --docs                Also search PDF, DOCX, ODT and RTF documents
      --json                Print results as JSON (the .garimpo format)
  -l, --files-only          Print only the paths of matching files
  -n, --dry-run             Show what replace would change without writing
  -h, --help                Show this help
)";

struct Options {
    QString command;
    SearchQuery query;
    std::optional<QString> replacement;
    QStringList folders;
    bool json = false;
    bool filesOnly = false;
    bool dryRun = false;
};

void printOut(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
}

void printErr(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stderr);
}

[[noreturn]] void fail(const QString &message)
{
    printErr(QStringLiteral("garimpo: %1\n").arg(message));
    std::exit(2);
}

Options parse(const QStringList &arguments)
{
    Options options;
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString &argument = arguments.at(i);
        auto value = [&]() -> QString {
            if (i + 1 >= arguments.size()) fail(QStringLiteral("%1 needs a value").arg(argument));
            return arguments.at(++i);
        };
        if (argument == QLatin1String("-h") || argument == QLatin1String("--help")) { printOut(QString::fromUtf8(kUsage)); std::exit(0); }
        else if (argument == QLatin1String("-p") || argument == QLatin1String("--pattern")) options.query.pattern = value();
        else if (argument == QLatin1String("-r") || argument == QLatin1String("--replace")) options.replacement = value();
        else if (argument == QLatin1String("-e") || argument == QLatin1String("--regex")) options.query.mode = PatternMode::Regex;
        else if (argument == QLatin1String("-x") || argument == QLatin1String("--expr")) options.query.mode = PatternMode::Expression;
        else if (argument == QLatin1String("--file-scope")) options.query.expressionScope = ExpressionScope::File;
        else if (argument == QLatin1String("-c") || argument == QLatin1String("--case-sensitive")) options.query.caseSensitive = true;
        else if (argument == QLatin1String("-w") || argument == QLatin1String("--word")) options.query.wholeWord = true;
        else if (argument == QLatin1String("-m") || argument == QLatin1String("--mask")) options.query.includeMasks = value();
        else if (argument == QLatin1String("--exclude")) options.query.excludeMasks = value();
        else if (argument == QLatin1String("--exclude-dir")) options.query.excludeDirectories = value();
        else if (argument == QLatin1String("--no-recurse")) options.query.recursive = false;
        else if (argument == QLatin1String("--hidden")) options.query.includeHidden = true;
        else if (argument == QLatin1String("--names")) options.query.fileNamesOnly = true;
        else if (argument == QLatin1String("--docs")) options.query.searchDocuments = true;
        else if (argument == QLatin1String("--json")) options.json = true;
        else if (argument == QLatin1String("-l") || argument == QLatin1String("--files-only")) options.filesOnly = true;
        else if (argument == QLatin1String("-n") || argument == QLatin1String("--dry-run")) options.dryRun = true;
        else if (argument.startsWith(QLatin1Char('-'))) fail(QStringLiteral("unknown option %1").arg(argument));
        else if (options.command.isEmpty()) options.command = argument;
        else options.folders.append(argument);
    }
    options.query.directories = options.folders.join(QLatin1Char(';'));
    return options;
}

ResultDocument collect(const SearchQuery &query)
{
    ResultDocument document;
    document.query = query;
    document.title = query.summary();
    try {
        document.stats = SearchEngine::run(query, SearchLimits{}, nullptr, &ExtractionCache::shared(),
                                           [&](std::vector<FileResult> &&files, const SearchStats &) {
                                               for (FileResult &file : files) document.files.push_back(std::move(file));
                                           });
    } catch (const PatternError &error) {
        fail(error.message());
    }
    std::sort(document.files.begin(), document.files.end(),
              [](const FileResult &a, const FileResult &b) { return a.path < b.path; });
    return document;
}

int runSearch(const Options &options)
{
    if (options.folders.isEmpty()) fail(QStringLiteral("search needs at least one folder"));
    const ResultDocument document = collect(options.query);
    if (options.json) {
        printOut(QString::fromUtf8(QJsonDocument(document.toJson()).toJson(QJsonDocument::Indented)));
    } else if (options.filesOnly || options.query.fileNamesOnly || options.query.pattern.isEmpty()) {
        for (const FileResult &file : document.files) printOut(file.path + QLatin1Char('\n'));
    } else {
        printOut(Exporter::text(document));
    }
    const SearchStats &stats = document.stats;
    printErr(QStringLiteral("%1 matches in %2 of %3 files (%4 s)%5\n")
                 .arg(stats.matchCount).arg(stats.filesMatched).arg(stats.filesScanned)
                 .arg(stats.durationSeconds, 0, 'f', 2)
                 .arg(stats.limitReached ? QStringLiteral(", limit reached") : QString()));
    if (stats.skippedDocuments > 0 && DocumentExtractor::pdfToTextPath().isEmpty())
        printErr(QStringLiteral("note: install poppler (pdftotext) to search PDF files\n"));
    return document.files.empty() ? 1 : 0;
}

int runReplace(const Options &options)
{
    if (!options.replacement) fail(QStringLiteral("replace needs -r TEXT"));
    if (options.folders.isEmpty()) fail(QStringLiteral("replace needs at least one folder"));
    if (options.query.pattern.isEmpty()) fail(QStringLiteral("replace needs -p PATTERN"));

    const ResultDocument document = collect(options.query);
    QStringList paths;
    for (const FileResult &file : document.files)
        if (file.kind == FileKind::Text) paths.append(file.path);

    ReplacePlan plan;
    try {
        plan = Replacer::plan(options.query, *options.replacement, paths);
    } catch (const PatternError &error) {
        fail(error.message());
    }

    for (const FileChange &file : plan.files) {
        for (const LineChange &line : file.lines) {
            printOut(QStringLiteral("%1:%2\n  - %3\n  + %4\n").arg(file.path).arg(line.lineNumber).arg(line.original, line.replaced));
        }
    }
    for (const Problem &skipped : plan.skipped) printErr(QStringLiteral("skipped %1: %2\n").arg(skipped.path, skipped.reason));
    if (options.dryRun) {
        printOut(QStringLiteral("%1 lines in %2 files would change (dry run)\n").arg(plan.selectedLineCount()).arg(plan.selectedFileCount()));
        return 0;
    }

    const ReplaceOutcome outcome = Replacer::execute(plan);
    for (const Problem &failure : outcome.failures) printErr(QStringLiteral("failed %1: %2\n").arg(failure.path, failure.reason));
    if (outcome.batch) {
        printOut(QStringLiteral("Changed %1 lines in %2 files. Run 'garimpo undo' to revert.\n")
                     .arg(outcome.batch->changedLines()).arg(outcome.batch->entries.size()));
    } else {
        printOut(QStringLiteral("Nothing changed.\n"));
    }
    return outcome.failures.empty() ? 0 : 1;
}

int runUndo()
{
    const auto batch = Replacer::lastBatch();
    if (!batch) {
        printOut(QStringLiteral("Nothing to undo.\n"));
        return 1;
    }
    const UndoOutcome outcome = Replacer::undo(*batch);
    for (const QString &path : outcome.restored) printOut(QStringLiteral("restored %1\n").arg(path));
    for (const QString &path : outcome.modifiedSince) printOut(QStringLiteral("kept %1 (edited after the replace)\n").arg(path));
    for (const Problem &failure : outcome.failures) printOut(QStringLiteral("failed %1: %2\n").arg(failure.path, failure.reason));
    return outcome.failures.empty() && outcome.modifiedSince.isEmpty() ? 0 : 1;
}

int runCache(const Options &options)
{
    ExtractionCache &cache = ExtractionCache::shared();
    const QString action = options.folders.value(0, QStringLiteral("size"));
    if (action == QLatin1String("clean")) {
        const auto report = cache.removeStale();
        printOut(QStringLiteral("Removed %1 stale entries, kept %2.\n").arg(report.removed).arg(report.kept));
    } else if (action == QLatin1String("clear")) {
        cache.clear();
        printOut(QStringLiteral("Cache cleared.\n"));
    } else if (action == QLatin1String("size")) {
        printOut(QLocale().formattedDataSize(cache.sizeOnDisk()) + QLatin1Char('\n'));
    } else {
        fail(QStringLiteral("cache expects clean, clear or size"));
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const Options options = parse(app.arguments().mid(1));
    if (options.command == QLatin1String("search")) return runSearch(options);
    if (options.command == QLatin1String("replace")) return runReplace(options);
    if (options.command == QLatin1String("undo")) return runUndo();
    if (options.command == QLatin1String("cache")) return runCache(options);
    if (options.command.isEmpty()) {
        printOut(QString::fromUtf8(kUsage));
        return 2;
    }
    fail(QStringLiteral("unknown command %1").arg(options.command));
}
