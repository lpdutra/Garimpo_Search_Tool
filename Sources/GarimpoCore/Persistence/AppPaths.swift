import Foundation

/// Locations of everything Garimpo stores on disk.
public enum AppPaths {
    /// `GARIMPO_HOME` overrides the base folder, which keeps tests away from real user data.
    public static var base: URL {
        if let custom = ProcessInfo.processInfo.environment["GARIMPO_HOME"], !custom.isEmpty {
            return URL(fileURLWithPath: custom, isDirectory: true)
        }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        return support.appendingPathComponent("Garimpo", isDirectory: true)
    }

    public static var history: URL { base.appendingPathComponent("history.json") }
    public static var session: URL { base.appendingPathComponent("session.json") }
    public static var cache: URL { base.appendingPathComponent("cache", isDirectory: true) }
    public static var backups: URL { base.appendingPathComponent("backups", isDirectory: true) }

    @discardableResult
    public static func ensure(_ directory: URL) -> URL {
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        return directory
    }

    public static func folderSize(_ directory: URL) -> Int64 {
        guard let enumerator = FileManager.default.enumerator(at: directory, includingPropertiesForKeys: [.fileSizeKey]) else { return 0 }
        var total: Int64 = 0
        for case let url as URL in enumerator {
            total += Int64((try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
        }
        return total
    }
}

enum JSONStore {
    static let encoder: JSONEncoder = {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        return encoder
    }()

    static let decoder: JSONDecoder = {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        return decoder
    }()

    static func load<T: Decodable>(_ type: T.Type, from url: URL) -> T? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        return try? decoder.decode(type, from: data)
    }

    static func save<T: Encodable>(_ value: T, to url: URL) throws {
        AppPaths.ensure(url.deletingLastPathComponent())
        try encoder.encode(value).write(to: url, options: .atomic)
    }
}
