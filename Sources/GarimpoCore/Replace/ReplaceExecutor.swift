import CryptoKit
import Foundation

/// Record of one replace run, kept next to its backups so it can be undone.
public struct ReplaceBatch: Codable, Sendable {
    public struct Entry: Codable, Sendable {
        public var path: String
        public var backupName: String
        /// Hash of the content written, used to detect edits made after the replace.
        public var writtenHash: String
        public var changedLines: Int
    }

    public var id: String
    public var date: Date
    public var pattern: String
    public var replacement: String
    public var entries: [Entry]

    public var changedLines: Int { entries.reduce(0) { $0 + $1.changedLines } }
}

public struct ReplaceOutcome: Sendable {
    public var batch: ReplaceBatch?
    public var failures: [(path: String, reason: String)]
}

public struct UndoOutcome: Sendable {
    public var restored: [String]
    /// Files left alone because they were edited after the replace.
    public var modifiedSince: [String]
    public var failures: [(path: String, reason: String)]
}

public enum ReplaceExecutor {
    private static let manifestName = "manifest.json"

    /// Writes the selected lines of `plan`, backing up every touched file first.
    public static func execute(_ plan: ReplacePlan, backupsRoot: URL = AppPaths.backups) -> ReplaceOutcome {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyyMMdd-HHmmss-SSS"
        let batchID = formatter.string(from: Date())
        let folder = backupsRoot.appendingPathComponent(batchID, isDirectory: true)

        var batch = ReplaceBatch(id: batchID, date: Date(), pattern: plan.query.pattern, replacement: plan.replacement, entries: [])
        var failures: [(String, String)] = []

        for (index, file) in plan.files.enumerated() where file.selectedLineCount > 0 {
            let url = URL(fileURLWithPath: file.path)
            do {
                let original = try Data(contentsOf: url)
                let decoded = try TextDecoder.decode(original)
                var lines = TextLines.split(decoded.text)
                var changed = 0
                for change in file.lines where change.included {
                    let position = change.lineNumber - 1
                    // The file may have changed since the preview; only touch lines that still match it.
                    guard lines.indices.contains(position), lines[position].content == change.original else { continue }
                    lines[position].content = change.replaced
                    changed += 1
                }
                guard changed > 0 else {
                    failures.append((file.path, "File changed since the preview"))
                    continue
                }
                guard let output = decoded.encode(TextLines.join(lines)) else {
                    failures.append((file.path, "New text cannot be saved as \(decoded.encodingName)"))
                    continue
                }

                AppPaths.ensure(folder)
                let backupName = String(format: "%04d-", index) + url.lastPathComponent
                try original.write(to: folder.appendingPathComponent(backupName))
                try output.write(to: url, options: .atomic)
                batch.entries.append(.init(path: file.path, backupName: backupName, writtenHash: hash(output), changedLines: changed))
            } catch {
                failures.append((file.path, error.localizedDescription))
            }
        }

        guard !batch.entries.isEmpty else { return ReplaceOutcome(batch: nil, failures: failures) }
        try? JSONStore.save(batch, to: folder.appendingPathComponent(manifestName))
        return ReplaceOutcome(batch: batch, failures: failures)
    }

    public static func batches(backupsRoot: URL = AppPaths.backups) -> [ReplaceBatch] {
        let folders = (try? FileManager.default.contentsOfDirectory(at: backupsRoot, includingPropertiesForKeys: nil)) ?? []
        return folders
            .compactMap { JSONStore.load(ReplaceBatch.self, from: $0.appendingPathComponent(manifestName)) }
            .sorted { $0.id > $1.id }
    }

    public static func lastBatch(backupsRoot: URL = AppPaths.backups) -> ReplaceBatch? {
        batches(backupsRoot: backupsRoot).first
    }

    /// Restores the backups of `batch` and deletes it. Files edited after the replace are kept unless `force` is set.
    public static func undo(_ batch: ReplaceBatch, force: Bool = false, backupsRoot: URL = AppPaths.backups) -> UndoOutcome {
        let folder = backupsRoot.appendingPathComponent(batch.id, isDirectory: true)
        var outcome = UndoOutcome(restored: [], modifiedSince: [], failures: [])

        for entry in batch.entries {
            let url = URL(fileURLWithPath: entry.path)
            if !force, let current = try? Data(contentsOf: url), hash(current) != entry.writtenHash {
                outcome.modifiedSince.append(entry.path)
                continue
            }
            do {
                let backup = try Data(contentsOf: folder.appendingPathComponent(entry.backupName))
                try backup.write(to: url, options: .atomic)
                outcome.restored.append(entry.path)
            } catch {
                outcome.failures.append((entry.path, error.localizedDescription))
            }
        }
        if outcome.modifiedSince.isEmpty, outcome.failures.isEmpty {
            try? FileManager.default.removeItem(at: folder)
        }
        return outcome
    }

    public static func clearBackups(backupsRoot: URL = AppPaths.backups) {
        try? FileManager.default.removeItem(at: backupsRoot)
    }

    static func hash(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }
}
