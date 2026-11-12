import Foundation

public struct FileCandidate: Sendable {
    public var url: URL
    public var size: Int64
    public var modified: Date?

    public init(url: URL, size: Int64, modified: Date?) {
        self.url = url
        self.size = size
        self.modified = modified
    }
}

/// Walks the query folders and yields the files that pass masks and filters.
public struct FileEnumerator: Sendable {
    public let query: SearchQuery

    public init(query: SearchQuery) {
        self.query = query
    }

    private static let keys: [URLResourceKey] = [
        .isDirectoryKey, .isRegularFileKey, .isHiddenKey, .fileSizeKey, .contentModificationDateKey,
    ]

    public func forEachFile(_ body: (FileCandidate) -> Bool) {
        var visited = Set<String>()
        for directory in query.directoryList {
            let root = URL(fileURLWithPath: directory).standardizedFileURL
            var isDirectory: ObjCBool = false
            guard FileManager.default.fileExists(atPath: root.path, isDirectory: &isDirectory) else { continue }

            if !isDirectory.boolValue {
                if let candidate = candidate(for: root), visited.insert(root.path).inserted, !body(candidate) { return }
                continue
            }

            var options: FileManager.DirectoryEnumerationOptions = []
            if !query.includeHidden { options.insert(.skipsHiddenFiles) }
            if !query.recursive { options.insert(.skipsSubdirectoryDescendants) }

            guard let enumerator = FileManager.default.enumerator(
                at: root, includingPropertiesForKeys: Self.keys, options: options, errorHandler: { _, _ in true }
            ) else { continue }

            for case let url as URL in enumerator {
                if Task.isCancelled { return }
                let values = try? url.resourceValues(forKeys: Set(Self.keys))
                if values?.isDirectory == true {
                    if Self.matchesAny(url.lastPathComponent, query.excludeDirectoryList) {
                        enumerator.skipDescendants()
                    }
                    continue
                }
                guard values?.isRegularFile == true, let candidate = candidate(for: url, values: values) else { continue }
                guard visited.insert(url.path).inserted else { continue }
                if !body(candidate) { return }
            }
        }
    }

    func candidate(for url: URL, values: URLResourceValues? = nil) -> FileCandidate? {
        let values = values ?? (try? url.resourceValues(forKeys: Set(Self.keys)))
        let name = url.lastPathComponent
        let includes = query.includeMaskList
        if !includes.isEmpty, !Self.matchesAny(name, includes) { return nil }
        if Self.matchesAny(name, query.excludeMaskList) { return nil }

        let size = Int64(values?.fileSize ?? 0)
        let modified = values?.contentModificationDate
        if let min = query.minSizeKB, size < Int64(min) * 1024 { return nil }
        if let max = query.maxSizeKB, size > Int64(max) * 1024 { return nil }
        if let after = query.modifiedAfter, let modified, modified < after { return nil }
        if let before = query.modifiedBefore, let modified, modified > before { return nil }
        return FileCandidate(url: url, size: size, modified: modified)
    }

    public static func matchesAny(_ name: String, _ patterns: [String]) -> Bool {
        patterns.contains { fnmatch($0, name, FNM_CASEFOLD) == 0 }
    }
}
