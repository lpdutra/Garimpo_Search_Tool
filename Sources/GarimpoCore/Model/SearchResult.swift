import Foundation

/// A UTF-16 range inside a line, compatible with `NSRange`.
public struct TextSpan: Codable, Hashable, Sendable {
    public var location: Int
    public var length: Int

    public init(location: Int, length: Int) {
        self.location = location
        self.length = length
    }

    public init(_ range: NSRange) {
        self.init(location: range.location, length: range.length)
    }

    public var nsRange: NSRange { NSRange(location: location, length: length) }
}

public struct LineMatch: Codable, Hashable, Identifiable, Sendable {
    public var id = UUID()
    /// 1-based line number.
    public var lineNumber: Int
    public var text: String
    public var spans: [TextSpan]

    public init(lineNumber: Int, text: String, spans: [TextSpan]) {
        self.lineNumber = lineNumber
        self.text = text
        self.spans = spans
    }

    /// 1-based column of the first span.
    public var column: Int { (spans.first?.location ?? 0) + 1 }
}

public enum FileKind: String, Codable, Sendable {
    case text
    case document
}

public struct FileResult: Codable, Hashable, Identifiable, Sendable {
    public var id = UUID()
    public var path: String
    public var size: Int64
    public var modified: Date?
    public var kind: FileKind
    public var encoding: String?
    public var matches: [LineMatch]
    /// Spans inside the file name, used when searching names only.
    public var nameSpans: [TextSpan]
    /// True when the match list was cut to respect the per-file limit.
    public var truncated: Bool

    public init(path: String, size: Int64, modified: Date?, kind: FileKind, encoding: String? = nil,
                matches: [LineMatch] = [], nameSpans: [TextSpan] = [], truncated: Bool = false) {
        self.path = path
        self.size = size
        self.modified = modified
        self.kind = kind
        self.encoding = encoding
        self.matches = matches
        self.nameSpans = nameSpans
        self.truncated = truncated
    }

    public var url: URL { URL(fileURLWithPath: path) }
    public var name: String { (path as NSString).lastPathComponent }
    public var directory: String { (path as NSString).deletingLastPathComponent }
    public var matchCount: Int { matches.reduce(0) { $0 + max($1.spans.count, 1) } }
}

public struct SearchStats: Codable, Hashable, Sendable {
    public var filesScanned = 0
    public var filesMatched = 0
    public var matchCount = 0
    public var skippedBinary = 0
    public var skippedUnreadable = 0
    public var duration: TimeInterval = 0
    public var limitReached = false

    public init() {}
}
