import Foundation

public enum PatternMode: String, Codable, CaseIterable, Sendable {
    case literal
    case regex
    case expression

    public var title: String {
        switch self {
        case .literal: "Text"
        case .regex: "Regex"
        case .expression: "Expression"
        }
    }
}

public enum ExpressionScope: String, Codable, CaseIterable, Sendable {
    case line
    case file

    public var title: String {
        switch self {
        case .line: "Same line"
        case .file: "Same file"
        }
    }
}

public struct SearchQuery: Codable, Hashable, Sendable {
    /// One or more folders separated by `;`.
    public var directories: String = ""
    /// Glob patterns separated by `;`, e.g. `*.swift;*.md`. Empty means every file.
    public var includeMasks: String = "*"
    public var excludeMasks: String = ""
    public var excludeDirectories: String = ".git;.svn;.hg;node_modules;.build;build;bin;obj;.idea;.vscode"
    public var recursive: Bool = true
    public var includeHidden: Bool = false

    /// Empty pattern lists every file accepted by the masks.
    public var pattern: String = ""
    public var mode: PatternMode = .literal
    public var caseSensitive: Bool = false
    public var wholeWord: Bool = false
    public var expressionScope: ExpressionScope = .line
    /// Matches the pattern against file names instead of file contents.
    public var fileNamesOnly: Bool = false
    /// Extracts text from PDF and office documents before matching.
    public var searchDocuments: Bool = false

    public var minSizeKB: Int? = nil
    public var maxSizeKB: Int? = nil
    public var modifiedAfter: Date? = nil
    public var modifiedBefore: Date? = nil

    public init() {}

    public var directoryList: [String] {
        Self.split(directories).map { ($0 as NSString).expandingTildeInPath }
    }

    public var includeMaskList: [String] { Self.split(includeMasks) }
    public var excludeMaskList: [String] { Self.split(excludeMasks) }
    public var excludeDirectoryList: [String] { Self.split(excludeDirectories) }

    public var summary: String {
        if pattern.isEmpty { return includeMasks.isEmpty ? "All files" : includeMasks }
        return pattern
    }

    static func split(_ value: String) -> [String] {
        value.split(separator: ";")
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
    }
}
