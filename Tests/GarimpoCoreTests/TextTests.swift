import Foundation
import Testing
@testable import GarimpoCore

struct TextDecoderTests {
    @Test func utf8() throws {
        let decoded = try TextDecoder.decode(Data("ação".utf8))
        #expect(decoded.text == "ação")
        #expect(decoded.encoding == .utf8)
    }

    @Test func windows1252FallbackRoundTrips() throws {
        let data = try #require("preço médio".data(using: .windowsCP1252))
        let decoded = try TextDecoder.decode(data)
        #expect(decoded.text == "preço médio")
        #expect(decoded.encoding == .windowsCP1252)
        #expect(decoded.encode(decoded.text) == data)
    }

    @Test func utf8BOMIsKept() throws {
        let data = Data([0xEF, 0xBB, 0xBF]) + Data("hi".utf8)
        let decoded = try TextDecoder.decode(data)
        #expect(decoded.text == "hi")
        #expect(decoded.encode("ok") == Data([0xEF, 0xBB, 0xBF]) + Data("ok".utf8))
    }

    @Test func utf16WithBOM() throws {
        let data = Data([0xFF, 0xFE]) + "olá".data(using: .utf16LittleEndian)!
        #expect(try TextDecoder.decode(data).text == "olá")
    }

    @Test func binaryIsRejected() {
        #expect(throws: TextDecoder.Failure.self) { try TextDecoder.decode(Data([0x50, 0x4B, 0x00, 0x01])) }
    }
}

struct TextLinesTests {
    @Test func keepsMixedTerminators() {
        let text = "one\r\ntwo\nthree\rfour"
        let lines = TextLines.split(text)
        #expect(lines.map(\.content) == ["one", "two", "three", "four"])
        #expect(lines.map(\.terminator) == ["\r\n", "\n", "\r", ""])
        #expect(TextLines.join(lines) == text)
    }

    @Test func trailingNewlineDoesNotAddEmptyLine() {
        #expect(TextLines.split("a\nb\n").count == 2)
        #expect(TextLines.split("").count == 1)
        #expect(TextLines.split("\n\n").map(\.content) == ["", ""])
    }

    @Test func lineOffsetsAreUTF16() {
        let lines = TextLines.split("é😀\r\nx")
        #expect(TextLines.lineOffsets(lines) == [0, 5])
    }
}

struct HistoryTests {
    @Test func mostRecentFirstWithPinnedOnTop() {
        var history = History()
        history.remember("/a", in: .directories)
        history.remember("/b", in: .directories)
        history.setPinned(true, value: "/a", in: .directories)
        history.remember("/c", in: .directories)
        #expect(history.values(.directories) == ["/a", "/c", "/b"])
    }

    @Test func limitKeepsPinned() {
        var history = History()
        history.limit = 2
        history.remember("pinned", in: .patterns)
        history.setPinned(true, value: "pinned", in: .patterns)
        for value in ["1", "2", "3"] { history.remember(value, in: .patterns) }
        #expect(history.values(.patterns) == ["pinned", "3"])
        history.clear(.patterns)
        #expect(history.values(.patterns) == ["pinned"])
    }

    @Test func roundTripsThroughDisk() throws {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".json")
        defer { try? FileManager.default.removeItem(at: url) }
        var history = History()
        var query = SearchQuery()
        query.directories = "/tmp"
        query.pattern = "needle"
        history.remember(query, replacement: "thread")
        try history.save(to: url)

        let loaded = History.load(from: url)
        #expect(loaded.values(.directories) == ["/tmp"])
        #expect(loaded.values(.replacements) == ["thread"])
        #expect(loaded.lastQuery == query)
    }
}
