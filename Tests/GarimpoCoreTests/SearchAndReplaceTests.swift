import AppKit
import Foundation
import Testing
@testable import GarimpoCore

/// Creates an isolated folder tree for each test.
final class Sandbox {
    let root: URL

    init() throws {
        root = FileManager.default.temporaryDirectory
            .appendingPathComponent("garimpo-tests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    }

    deinit { try? FileManager.default.removeItem(at: root) }

    @discardableResult
    func write(_ relativePath: String, _ data: Data) throws -> URL {
        let url = root.appendingPathComponent(relativePath)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url)
        return url
    }

    @discardableResult
    func write(_ relativePath: String, _ text: String) throws -> URL {
        try write(relativePath, Data(text.utf8))
    }

    func query(_ pattern: String) -> SearchQuery {
        var query = SearchQuery()
        query.directories = root.path
        query.pattern = pattern
        return query
    }
}

func runSearch(_ query: SearchQuery, cache: ExtractionCache? = nil) throws -> (files: [FileResult], stats: SearchStats) {
    var files: [FileResult] = []
    let stats = try SearchEngine.search(query, cache: cache) { event in
        if case .found(let found) = event { files += found }
    }
    return (files.sorted { $0.path < $1.path }, stats)
}

struct SearchEngineTests {
    @Test func masksExclusionsAndBinaries() throws {
        let box = try Sandbox()
        try box.write("src/main.swift", "let target = 1")
        try box.write("src/readme.md", "target in docs")
        try box.write("node_modules/lib.swift", "target hidden in deps")
        try box.write(".hidden/secret.swift", "target")
        try box.write("src/image.swift", Data([0x74, 0x61, 0x00, 0x72, 0x67]))

        var query = box.query("target")
        query.includeMasks = "*.swift"
        let (files, stats) = try runSearch(query)
        #expect(files.map(\.name) == ["main.swift"])
        #expect(stats.skippedBinary == 1)
        #expect(stats.matchCount == 1)
    }

    @Test func nonRecursiveAndHidden() throws {
        let box = try Sandbox()
        try box.write("top.txt", "x")
        try box.write("sub/deep.txt", "x")
        try box.write(".dot/inner.txt", "x")

        var query = box.query("x")
        query.recursive = false
        #expect(try runSearch(query).files.map(\.name) == ["top.txt"])

        query.recursive = true
        query.includeHidden = true
        query.excludeDirectories = ""
        #expect(try runSearch(query).files.count == 3)
    }

    @Test func emptyPatternListsFiles() throws {
        let box = try Sandbox()
        try box.write("a.log", "")
        try box.write("b.txt", "")
        var query = box.query("")
        query.includeMasks = "*.log"
        let files = try runSearch(query).files
        #expect(files.map(\.name) == ["a.log"])
        #expect(files[0].matches.isEmpty)
    }

    @Test func fileNamesOnly() throws {
        let box = try Sandbox()
        try box.write("Invoice-2024.txt", "nothing")
        try box.write("other.txt", "invoice inside")
        var query = box.query("invoice")
        query.fileNamesOnly = true
        let files = try runSearch(query).files
        #expect(files.map(\.name) == ["Invoice-2024.txt"])
        #expect(files[0].nameSpans == [TextSpan(location: 0, length: 7)])
    }

    @Test func legacyEncodingIsSearchable() throws {
        let box = try Sandbox()
        try box.write("legacy.p", try #require("/* cobrança */".data(using: .windowsCP1252)))
        let files = try runSearch(box.query("cobrança")).files
        #expect(files.count == 1)
        #expect(files[0].encoding == "Windows-1252")
    }

    @Test func sizeFilter() throws {
        let box = try Sandbox()
        try box.write("small.txt", "hit")
        try box.write("big.txt", "hit" + String(repeating: " ", count: 4096))
        var query = box.query("hit")
        query.maxSizeKB = 2
        #expect(try runSearch(query).files.map(\.name) == ["small.txt"])
    }

    @Test func missingFolderThrows() {
        var query = SearchQuery()
        query.pattern = "x"
        #expect(throws: SearchEngineError.self) { try runSearch(query) }
    }

    @Test func documentsAreExtractedAndCached() throws {
        let box = try Sandbox()
        let rtf = try NSAttributedString(string: "Quarterly revenue grew").data(
            from: NSRange(location: 0, length: 22), documentAttributes: [.documentType: NSAttributedString.DocumentType.rtf])
        try box.write("report.rtf", rtf)
        let cache = ExtractionCache(directory: box.root.appendingPathComponent(".cache"))

        var query = box.query("revenue")
        query.searchDocuments = true
        query.excludeDirectories = ".cache"
        let files = try runSearch(query, cache: cache).files
        #expect(files.map(\.kind) == [.document])
        #expect(files[0].matches.first?.text == "Quarterly revenue grew")
        #expect(cache.removeStale().kept == 1)

        try FileManager.default.removeItem(at: box.root.appendingPathComponent("report.rtf"))
        #expect(cache.removeStale().removed == 1)
    }
}

struct ReplaceTests {
    @Test func replacePreservesEncodingAndLineEndingsThenUndo() throws {
        let box = try Sandbox()
        let original = try #require("preço = 1\r\nmanter\r\npreço = 2\r\n".data(using: .windowsCP1252))
        let file = try box.write("prices.txt", original)
        let backups = box.root.appendingPathComponent(".backups")

        let plan = try ReplacePlanner.plan(query: box.query("preço"), replacement: "valor", paths: [file.path])
        #expect(plan.files.count == 1)
        #expect(plan.selectedLineCount == 2)

        let outcome = ReplaceExecutor.execute(plan, backupsRoot: backups)
        let batch = try #require(outcome.batch)
        #expect(outcome.failures.isEmpty)
        #expect(try Data(contentsOf: file) == "valor = 1\r\nmanter\r\nvalor = 2\r\n".data(using: .windowsCP1252))
        #expect(ReplaceExecutor.lastBatch(backupsRoot: backups)?.id == batch.id)

        let undo = ReplaceExecutor.undo(batch, backupsRoot: backups)
        #expect(undo.restored == [file.path])
        #expect(try Data(contentsOf: file) == original)
        #expect(ReplaceExecutor.lastBatch(backupsRoot: backups) == nil)
    }

    @Test func regexGroupsAndLineSelection() throws {
        let box = try Sandbox()
        let file = try box.write("names.txt", "Doe, John\nRoe, Jane\n")
        var query = box.query(#"(\w+), (\w+)"#)
        query.mode = .regex

        var plan = try ReplacePlanner.plan(query: query, replacement: "$2 $1", paths: [file.path])
        #expect(plan.files[0].lines.map(\.replaced) == ["John Doe", "Jane Roe"])
        plan.files[0].lines[1].included = false

        _ = ReplaceExecutor.execute(plan, backupsRoot: box.root.appendingPathComponent(".backups"))
        #expect(try String(contentsOf: file, encoding: .utf8) == "John Doe\nRoe, Jane\n")
    }

    @Test func literalReplacementIsNotATemplate() throws {
        let box = try Sandbox()
        let file = try box.write("cost.txt", "cost")
        let plan = try ReplacePlanner.plan(query: box.query("cost"), replacement: "$1.00", paths: [file.path])
        #expect(plan.files[0].lines[0].replaced == "$1.00")
    }

    @Test func undoSkipsFilesEditedAfterwards() throws {
        let box = try Sandbox()
        let file = try box.write("a.txt", "old")
        let backups = box.root.appendingPathComponent(".backups")
        let plan = try ReplacePlanner.plan(query: box.query("old"), replacement: "new", paths: [file.path])
        let batch = try #require(ReplaceExecutor.execute(plan, backupsRoot: backups).batch)

        try "edited by hand".write(to: file, atomically: true, encoding: .utf8)
        let undo = ReplaceExecutor.undo(batch, backupsRoot: backups)
        #expect(undo.modifiedSince == [file.path])
        #expect(try String(contentsOf: file, encoding: .utf8) == "edited by hand")
    }

    @Test func expressionModeCannotReplace() {
        var query = SearchQuery()
        query.pattern = "a AND b"
        query.mode = .expression
        #expect(throws: MatcherError.replaceNotSupported) {
            try ReplacePlanner.plan(query: query, replacement: "x", paths: [])
        }
    }
}

struct ExportTests {
    @Test func formats() {
        var file = FileResult(path: "/p/a.txt", size: 1, modified: nil, kind: .text)
        file.matches = [LineMatch(lineNumber: 3, text: "say \"<hi>\"", spans: [TextSpan(location: 5, length: 4)])]
        let document = ResultDocument(title: "t", query: SearchQuery(), files: [file], stats: nil)

        #expect(ResultExporter.text(document) == "/p/a.txt:3:6: say \"<hi>\"\n")
        #expect(ResultExporter.csv(document).hasSuffix("\"/p/a.txt\",3,6,\"say \"\"<hi>\"\"\"\n"))
        #expect(ResultExporter.html(document).contains("say &quot;<mark>&lt;hi&gt;</mark>&quot;"))
    }

    @Test func documentRoundTrip() throws {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".garimpo")
        defer { try? FileManager.default.removeItem(at: url) }
        var query = SearchQuery()
        query.pattern = "abc"
        let document = ResultDocument(title: "Saved", query: query,
                                      files: [FileResult(path: "/x", size: 0, modified: Date(), kind: .text)], stats: SearchStats())
        try document.write(to: url)
        let loaded = try ResultDocument.read(from: url)
        #expect(loaded.title == "Saved")
        #expect(loaded.query == query)
        #expect(loaded.files.map(\.path) == ["/x"])
    }
}
