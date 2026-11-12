import Foundation
import GarimpoCore

let usage = """
usage: garimpo <command> [options]

commands:
  search  <folder>... -p PATTERN [options]   Search file contents (or list files when -p is omitted)
  replace <folder>... -p PATTERN -r TEXT     Replace text, backing up every changed file
  undo                                       Restore the files changed by the last replace
  cache   clean|clear|size                   Manage the document text cache

options:
  -p, --pattern TEXT        Pattern to search for
  -r, --replace TEXT        Replacement text (regex mode accepts $1, $2...)
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
      --docs                Also search PDF, RTF, DOC, DOCX and ODT documents
      --json                Print results as JSON
  -l, --files-only          Print only the paths of matching files
  -n, --dry-run             Show what replace would change without writing
  -h, --help                Show this help
"""

struct Options {
    var command = ""
    var query = SearchQuery()
    var replacement: String?
    var json = false
    var filesOnly = false
    var dryRun = false
    var positional: [String] = []
}

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("garimpo: \(message)\n".utf8))
    exit(2)
}

func parse(_ arguments: [String]) -> Options {
    var options = Options()
    var iterator = arguments.makeIterator()
    var folders: [String] = []

    func value(for flag: String) -> String {
        guard let value = iterator.next() else { fail("\(flag) needs a value") }
        return value
    }

    while let argument = iterator.next() {
        switch argument {
        case "-h", "--help": print(usage); exit(0)
        case "-p", "--pattern": options.query.pattern = value(for: argument)
        case "-r", "--replace": options.replacement = value(for: argument)
        case "-e", "--regex": options.query.mode = .regex
        case "-x", "--expr": options.query.mode = .expression
        case "--file-scope": options.query.expressionScope = .file
        case "-c", "--case-sensitive": options.query.caseSensitive = true
        case "-w", "--word": options.query.wholeWord = true
        case "-m", "--mask": options.query.includeMasks = value(for: argument)
        case "--exclude": options.query.excludeMasks = value(for: argument)
        case "--exclude-dir": options.query.excludeDirectories = value(for: argument)
        case "--no-recurse": options.query.recursive = false
        case "--hidden": options.query.includeHidden = true
        case "--names": options.query.fileNamesOnly = true
        case "--docs": options.query.searchDocuments = true
        case "--json": options.json = true
        case "-l", "--files-only": options.filesOnly = true
        case "-n", "--dry-run": options.dryRun = true
        default:
            if argument.hasPrefix("-") { fail("unknown option \(argument)") }
            if options.command.isEmpty { options.command = argument } else { folders.append(argument) }
        }
    }
    options.positional = folders
    options.query.directories = folders.joined(separator: ";")
    return options
}

func collect(_ query: SearchQuery) -> ([FileResult], SearchStats) {
    var files: [FileResult] = []
    do {
        let stats = try SearchEngine.search(query) { event in
            if case .found(let found) = event { files += found }
        }
        return (files.sorted { $0.path < $1.path }, stats)
    } catch {
        fail(error.localizedDescription)
    }
}

func printJSON<T: Encodable>(_ value: T) {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
    encoder.dateEncodingStrategy = .iso8601
    guard let data = try? encoder.encode(value) else { fail("could not encode output") }
    FileHandle.standardOutput.write(data + Data("\n".utf8))
}

func runSearch(_ options: Options) -> Int32 {
    if options.positional.isEmpty { fail("search needs at least one folder") }
    let (files, stats) = collect(options.query)
    if options.json {
        printJSON(ResultDocument(title: options.query.summary, query: options.query, files: files, stats: stats))
    } else if options.filesOnly || options.query.fileNamesOnly || options.query.pattern.isEmpty {
        files.forEach { print($0.path) }
    } else {
        let document = ResultDocument(title: options.query.summary, query: options.query, files: files, stats: stats)
        print(ResultExporter.text(document), terminator: "")
    }
    let note = stats.limitReached ? " (limit reached)" : ""
    FileHandle.standardError.write(Data(
        "\(stats.matchCount) matches in \(stats.filesMatched) of \(stats.filesScanned) files\(note)\n".utf8))
    return files.isEmpty ? 1 : 0
}

func runReplace(_ options: Options) -> Int32 {
    guard let replacement = options.replacement else { fail("replace needs -r TEXT") }
    if options.positional.isEmpty { fail("replace needs at least one folder") }
    if options.query.pattern.isEmpty { fail("replace needs -p PATTERN") }

    let (files, _) = collect(options.query)
    let plan: ReplacePlan
    do {
        plan = try ReplacePlanner.plan(query: options.query, replacement: replacement, paths: files.map(\.path))
    } catch {
        fail(error.localizedDescription)
    }

    for file in plan.files {
        for line in file.lines {
            print("\(file.path):\(line.lineNumber)")
            print("  - \(line.original)")
            print("  + \(line.replaced)")
        }
    }
    for skipped in plan.skipped {
        FileHandle.standardError.write(Data("skipped \(skipped.path): \(skipped.reason)\n".utf8))
    }
    if options.dryRun {
        print("\(plan.selectedLineCount) lines in \(plan.selectedFileCount) files would change (dry run)")
        return 0
    }

    let outcome = ReplaceExecutor.execute(plan)
    for failure in outcome.failures {
        FileHandle.standardError.write(Data("failed \(failure.path): \(failure.reason)\n".utf8))
    }
    if let batch = outcome.batch {
        print("Changed \(batch.changedLines) lines in \(batch.entries.count) files. Run 'garimpo undo' to revert.")
    } else {
        print("Nothing changed.")
    }
    return outcome.failures.isEmpty ? 0 : 1
}

func runUndo() -> Int32 {
    guard let batch = ReplaceExecutor.lastBatch() else {
        print("Nothing to undo.")
        return 1
    }
    let outcome = ReplaceExecutor.undo(batch)
    outcome.restored.forEach { print("restored \($0)") }
    outcome.modifiedSince.forEach { print("kept \($0) (edited after the replace)") }
    outcome.failures.forEach { print("failed \($0.path): \($0.reason)") }
    return outcome.failures.isEmpty && outcome.modifiedSince.isEmpty ? 0 : 1
}

func runCache(_ options: Options) -> Int32 {
    let cache = ExtractionCache.shared
    switch options.positional.first ?? "size" {
    case "clean":
        let report = cache.removeStale()
        print("Removed \(report.removed) stale entries, kept \(report.kept).")
    case "clear":
        cache.clear()
        print("Cache cleared.")
    case "size":
        print(ByteCountFormatter.string(fromByteCount: cache.sizeOnDisk, countStyle: .file))
    default:
        fail("cache expects clean, clear or size")
    }
    return 0
}

let options = parse(Array(CommandLine.arguments.dropFirst()))
switch options.command {
case "search": exit(runSearch(options))
case "replace": exit(runReplace(options))
case "undo": exit(runUndo())
case "cache": exit(runCache(options))
case "": print(usage); exit(2)
default: fail("unknown command \(options.command)")
}
