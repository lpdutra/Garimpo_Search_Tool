import CryptoKit
import Foundation

/// Disk cache for text extracted from documents, so repeated searches skip the slow conversion.
public final class ExtractionCache: @unchecked Sendable {
    public static let shared = ExtractionCache(directory: AppPaths.cache)

    struct Entry: Codable {
        var path: String
        var size: Int64
        var modified: Date?
        var text: String
    }

    public struct CleanupReport: Sendable {
        public var removed = 0
        public var kept = 0
    }

    private let directory: URL
    private let lock = NSLock()

    public init(directory: URL) {
        self.directory = directory
    }

    /// Returns cached text when the file is unchanged, otherwise extracts and stores it.
    public func text(for candidate: FileCandidate) -> String? {
        let entryURL = entryURL(for: candidate.url.path)
        if let entry = JSONStore.load(Entry.self, from: entryURL),
           entry.size == candidate.size, Self.sameDate(entry.modified, candidate.modified) {
            return entry.text
        }
        guard let text = DocumentExtractor.extractText(from: candidate.url) else { return nil }
        let entry = Entry(path: candidate.url.path, size: candidate.size, modified: candidate.modified, text: text)
        lock.withLock { try? JSONStore.save(entry, to: entryURL) }
        return text
    }

    /// Drops entries whose source file is gone or has changed.
    @discardableResult
    public func removeStale() -> CleanupReport {
        var report = CleanupReport()
        let files = (try? FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)) ?? []
        for file in files where file.pathExtension == "json" {
            guard let entry = JSONStore.load(Entry.self, from: file) else {
                try? FileManager.default.removeItem(at: file)
                report.removed += 1
                continue
            }
            let attributes = try? FileManager.default.attributesOfItem(atPath: entry.path)
            let size = (attributes?[.size] as? NSNumber)?.int64Value
            let modified = attributes?[.modificationDate] as? Date
            if attributes == nil || size != entry.size || !Self.sameDate(modified, entry.modified) {
                try? FileManager.default.removeItem(at: file)
                report.removed += 1
            } else {
                report.kept += 1
            }
        }
        return report
    }

    public func clear() {
        try? FileManager.default.removeItem(at: directory)
    }

    public var sizeOnDisk: Int64 { AppPaths.folderSize(directory) }

    private func entryURL(for path: String) -> URL {
        let digest = SHA256.hash(data: Data(path.utf8)).map { String(format: "%02x", $0) }.joined()
        return directory.appendingPathComponent(digest).appendingPathExtension("json")
    }

    /// ISO-8601 storage drops sub-second precision.
    private static func sameDate(_ lhs: Date?, _ rhs: Date?) -> Bool {
        guard let lhs, let rhs else { return lhs == nil && rhs == nil }
        return abs(lhs.timeIntervalSince(rhs)) < 1
    }
}
