import Foundation

/// A saved search: the query plus its results, stored as JSON in `.garimpo` files.
public struct ResultDocument: Codable, Sendable {
    public static let fileExtension = "garimpo"
    public static let currentVersion = 1

    public var version = ResultDocument.currentVersion
    public var title: String
    public var query: SearchQuery
    public var createdAt: Date
    public var files: [FileResult]
    public var stats: SearchStats?

    public init(title: String, query: SearchQuery, createdAt: Date = Date(), files: [FileResult], stats: SearchStats?) {
        self.title = title
        self.query = query
        self.createdAt = createdAt
        self.files = files
        self.stats = stats
    }

    public func write(to url: URL) throws {
        try JSONStore.save(self, to: url)
    }

    public static func read(from url: URL) throws -> ResultDocument {
        let data = try Data(contentsOf: url)
        return try JSONStore.decoder.decode(ResultDocument.self, from: data)
    }
}

/// Open tabs restored on the next launch.
public struct Session: Codable, Sendable {
    public var tabs: [ResultDocument]
    public var selectedIndex: Int?

    public init(tabs: [ResultDocument], selectedIndex: Int?) {
        self.tabs = tabs
        self.selectedIndex = selectedIndex
    }

    public static func load(from url: URL = AppPaths.session) -> Session? {
        JSONStore.load(Session.self, from: url)
    }

    public func save(to url: URL = AppPaths.session) throws {
        try JSONStore.save(self, to: url)
    }
}
