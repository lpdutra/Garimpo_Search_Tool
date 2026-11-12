import Foundation

public struct SearchLimits: Sendable {
    public var maxMatchesPerFile = 5_000
    public var maxTotalMatches = 200_000
    /// Larger plain-text files are skipped.
    public var maxFileSizeMB = 64

    public init(maxMatchesPerFile: Int = 5_000, maxTotalMatches: Int = 200_000, maxFileSizeMB: Int = 64) {
        self.maxMatchesPerFile = maxMatchesPerFile
        self.maxTotalMatches = maxTotalMatches
        self.maxFileSizeMB = maxFileSizeMB
    }
}

public enum SearchEvent: Sendable {
    case found([FileResult])
    case progress(SearchStats)
    case finished(SearchStats)
}

public enum SearchEngineError: LocalizedError {
    case noDirectories

    public var errorDescription: String? {
        "Choose at least one folder to search."
    }
}

public enum SearchEngine {
    private static let batchSize = 64

    /// Streams results while the search runs. Cancelling the consuming task stops the search.
    public static func run(
        _ query: SearchQuery,
        limits: SearchLimits = SearchLimits(),
        cache: ExtractionCache? = .shared
    ) -> AsyncThrowingStream<SearchEvent, Error> {
        AsyncThrowingStream { continuation in
            let task = Task.detached(priority: .userInitiated) {
                do {
                    let stats = try search(query, limits: limits, cache: cache) { event in
                        continuation.yield(event)
                    }
                    continuation.yield(.finished(stats))
                    continuation.finish()
                } catch {
                    continuation.finish(throwing: error)
                }
            }
            continuation.onTermination = { _ in task.cancel() }
        }
    }

    /// Synchronous search used by the stream and the command-line tool.
    public static func search(
        _ query: SearchQuery,
        limits: SearchLimits = SearchLimits(),
        cache: ExtractionCache? = .shared,
        emit: (SearchEvent) -> Void
    ) throws -> SearchStats {
        guard !query.directoryList.isEmpty else { throw SearchEngineError.noDirectories }
        let matcher: Matcher? = query.pattern.isEmpty ? nil : try Matcher(query: query)
        let started = Date()
        var stats = SearchStats()
        var batch: [FileCandidate] = []
        var lastProgress = Date.distantPast

        func flush() {
            let processed = process(batch, query: query, matcher: matcher, limits: limits, cache: cache)
            batch.removeAll(keepingCapacity: true)

            var accepted: [FileResult] = []
            for outcome in processed {
                stats.filesScanned += 1
                switch outcome {
                case .binary: stats.skippedBinary += 1
                case .unreadable: stats.skippedUnreadable += 1
                case .noMatch: break
                case .match(var file):
                    let remaining = limits.maxTotalMatches - stats.matchCount
                    guard remaining > 0 else { stats.limitReached = true; continue }
                    if file.matches.count > remaining {
                        file.matches = Array(file.matches.prefix(remaining))
                        file.truncated = true
                        stats.limitReached = true
                    }
                    stats.filesMatched += 1
                    stats.matchCount += file.matchCount
                    accepted.append(file)
                }
            }
            if !accepted.isEmpty { emit(.found(accepted)) }
            if Date().timeIntervalSince(lastProgress) > 0.1 {
                stats.duration = Date().timeIntervalSince(started)
                emit(.progress(stats))
                lastProgress = Date()
            }
        }

        FileEnumerator(query: query).forEachFile { candidate in
            batch.append(candidate)
            if batch.count >= batchSize { flush() }
            return !Task.isCancelled && !stats.limitReached
        }
        if !batch.isEmpty, !Task.isCancelled { flush() }

        stats.duration = Date().timeIntervalSince(started)
        return stats
    }

    enum Outcome {
        case match(FileResult)
        case noMatch
        case binary
        case unreadable
    }

    private static func process(
        _ candidates: [FileCandidate], query: SearchQuery, matcher: Matcher?, limits: SearchLimits, cache: ExtractionCache?
    ) -> [Outcome] {
        var outcomes = [Outcome](repeating: .noMatch, count: candidates.count)
        outcomes.withUnsafeMutableBufferPointer { buffer in
            let base = buffer.baseAddress!
            DispatchQueue.concurrentPerform(iterations: candidates.count) { index in
                guard !Task.isCancelled else { return }
                (base + index).pointee = examine(candidates[index], query: query, matcher: matcher, limits: limits, cache: cache)
            }
        }
        return outcomes
    }

    static func examine(
        _ candidate: FileCandidate, query: SearchQuery, matcher: Matcher?, limits: SearchLimits, cache: ExtractionCache?
    ) -> Outcome {
        var file = FileResult(path: candidate.url.path, size: candidate.size, modified: candidate.modified, kind: .text)

        guard let matcher else { return .match(file) }

        if query.fileNamesOnly {
            guard let spans = matcher.nameSpans(file.name) else { return .noMatch }
            file.nameSpans = spans
            return .match(file)
        }

        let text: String
        if query.searchDocuments, DocumentExtractor.canExtract(candidate.url) {
            let extracted = cache?.text(for: candidate) ?? DocumentExtractor.extractText(from: candidate.url)
            guard let extracted else { return .unreadable }
            text = extracted
            file.kind = .document
        } else {
            guard candidate.size <= Int64(limits.maxFileSizeMB) * 1_048_576 else { return .unreadable }
            do {
                let decoded = try TextDecoder.decode(contentsOf: candidate.url)
                text = decoded.text
                file.encoding = decoded.encodingName
            } catch TextDecoder.Failure.binary {
                return .binary
            } catch {
                return .unreadable
            }
        }

        let lines = TextLines.split(text)
        let result = matcher.match(lines: lines, limit: limits.maxMatchesPerFile)
        if result.matches.isEmpty {
            // File-scope expressions made only of negations match without any line to highlight.
            return query.mode == .expression && query.expressionScope == .file && matcher.fileMatches(lines: lines)
                ? .match(file) : .noMatch
        }
        file.matches = result.matches
        file.truncated = result.truncated
        return .match(file)
    }
}
