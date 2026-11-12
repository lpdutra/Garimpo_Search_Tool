import Foundation

public enum HistoryField: String, Codable, CodingKeyRepresentable, CaseIterable, Sendable {
    case directories
    case includeMasks
    case excludeMasks
    case patterns
    case replacements
}

public struct HistoryEntry: Codable, Hashable, Sendable {
    public var value: String
    public var pinned: Bool
    public var lastUsed: Date

    public init(value: String, pinned: Bool = false, lastUsed: Date = Date()) {
        self.value = value
        self.pinned = pinned
        self.lastUsed = lastUsed
    }
}

/// Most-recently-used values for every form field, plus the last query, saved between launches.
public struct History: Codable, Sendable {
    public static let defaultLimit = 30

    public var lists: [HistoryField: [HistoryEntry]] = [:]
    public var lastQuery: SearchQuery?
    public var lastReplacement: String?
    public var limit = History.defaultLimit

    public init() {}

    /// Pinned entries first, then most recent.
    public func entries(_ field: HistoryField) -> [HistoryEntry] {
        (lists[field] ?? []).sorted {
            if $0.pinned != $1.pinned { return $0.pinned }
            return $0.lastUsed > $1.lastUsed
        }
    }

    public func values(_ field: HistoryField) -> [String] {
        entries(field).map(\.value)
    }

    public mutating func remember(_ value: String, in field: HistoryField) {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        var list = lists[field] ?? []
        if let index = list.firstIndex(where: { $0.value == trimmed }) {
            list[index].lastUsed = Date()
        } else {
            list.append(HistoryEntry(value: trimmed))
        }
        // Pinned entries never fall off the list.
        let pinned = list.filter(\.pinned)
        let recent = list.filter { !$0.pinned }.sorted { $0.lastUsed > $1.lastUsed }.prefix(max(0, limit - pinned.count))
        lists[field] = pinned + recent
    }

    /// Records every non-empty field of a query that was just run.
    public mutating func remember(_ query: SearchQuery, replacement: String? = nil) {
        remember(query.directories, in: .directories)
        remember(query.includeMasks, in: .includeMasks)
        remember(query.excludeMasks, in: .excludeMasks)
        remember(query.pattern, in: .patterns)
        if let replacement { remember(replacement, in: .replacements) }
        lastQuery = query
    }

    public func isPinned(_ value: String, in field: HistoryField) -> Bool {
        lists[field]?.contains { $0.value == value && $0.pinned } ?? false
    }

    public mutating func setPinned(_ pinned: Bool, value: String, in field: HistoryField) {
        var list = lists[field] ?? []
        if let index = list.firstIndex(where: { $0.value == value }) {
            list[index].pinned = pinned
        } else if pinned {
            list.append(HistoryEntry(value: value, pinned: true))
        }
        lists[field] = list
    }

    public mutating func remove(_ value: String, from field: HistoryField) {
        lists[field]?.removeAll { $0.value == value }
    }

    /// Clears a field, keeping pinned entries.
    public mutating func clear(_ field: HistoryField) {
        lists[field] = lists[field]?.filter(\.pinned)
    }

    public static func load(from url: URL = AppPaths.history) -> History {
        JSONStore.load(History.self, from: url) ?? History()
    }

    public func save(to url: URL = AppPaths.history) throws {
        try JSONStore.save(self, to: url)
    }
}
