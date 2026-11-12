#include "SearchEngine.h"

#include "TextCodec.h"

#include <QElapsedTimer>
#include <QThread>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace garimpo {

SearchEngine::Outcome SearchEngine::examine(const FileCandidate &candidate, const SearchQuery &query, const Matcher *matcher,
                                            const SearchLimits &limits, ExtractionCache *cache, FileResult &result)
{
    result.path = candidate.path;
    result.size = candidate.size;
    result.modifiedMSecs = candidate.modifiedMSecs;
    if (!matcher) return Outcome::Match;

    if (query.fileNamesOnly) {
        const qsizetype slash = candidate.path.lastIndexOf(QLatin1Char('/'));
        auto spans = matcher->matchName(candidate.path.mid(slash + 1));
        if (!spans) return Outcome::NoMatch;
        result.nameSpans = *spans;
        return Outcome::Match;
    }

    QString text;
    if (query.searchDocuments && DocumentExtractor::canExtract(candidate.path)) {
        auto extracted = cache ? cache->text(candidate) : DocumentExtractor::extractText(candidate.path);
        if (!extracted) return Outcome::DocumentSkipped;
        text = std::move(*extracted);
        result.kind = FileKind::Document;
    } else {
        DecodeError error = DecodeError::Unreadable;
        auto decoded = TextCodec::decodeFile(candidate.path, limits.maxFileSizeMB * 1024 * 1024, &error);
        if (!decoded) return error == DecodeError::Binary ? Outcome::Binary : Outcome::Unreadable;
        text = std::move(decoded->text);
        result.encoding = encodingName(decoded->encoding);
    }

    MatchResult match = matcher->match(text, limits.maxLinesPerFile);
    if (!match.fileMatches) return Outcome::NoMatch;
    result.matches = std::move(match.matches);
    result.truncated = match.truncated;
    return Outcome::Match;
}

SearchStats SearchEngine::run(const SearchQuery &query, const SearchLimits &limits, const std::atomic<bool> *cancel,
                              ExtractionCache *cache, const BatchHandler &onBatch)
{
    if (query.directoryList().isEmpty()) throw PatternError(QStringLiteral("Choose at least one folder to search."));
    std::optional<Matcher> matcher;
    if (!query.pattern.isEmpty()) matcher.emplace(query);

    QElapsedTimer clock;
    clock.start();

    // Every worker holds at most one file in memory, so few threads keep the footprint small.
    const int threads = limits.threads > 0 ? limits.threads : qBound(2, QThread::idealThreadCount(), 8);
    const size_t queueCapacity = size_t(threads) * 16;

    std::mutex mutex;
    std::condition_variable queueChanged;
    std::deque<FileCandidate> queue;
    bool producerDone = false;
    std::atomic<bool> stop{false};

    std::vector<FileResult> outbox;
    SearchStats stats;

    auto stopped = [&] { return stop.load(std::memory_order_relaxed) || (cancel && cancel->load(std::memory_order_relaxed)); };

    auto worker = [&] {
        for (;;) {
            FileCandidate candidate;
            {
                std::unique_lock lock(mutex);
                queueChanged.wait(lock, [&] { return !queue.empty() || producerDone || stopped(); });
                if (stopped() || queue.empty()) return;
                candidate = std::move(queue.front());
                queue.pop_front();
            }
            queueChanged.notify_all();

            FileResult result;
            const Outcome outcome = examine(candidate, query, matcher ? &*matcher : nullptr, limits, cache, result);

            std::lock_guard lock(mutex);
            ++stats.filesScanned;
            switch (outcome) {
            case Outcome::Binary: ++stats.skippedBinary; break;
            case Outcome::Unreadable: ++stats.skippedUnreadable; break;
            case Outcome::DocumentSkipped: ++stats.skippedDocuments; break;
            case Outcome::NoMatch: break;
            case Outcome::Match: {
                const qint64 remaining = limits.maxTotalMatches - stats.matchCount;
                if (remaining <= 0) {
                    stats.limitReached = true;
                    stop = true;
                    break;
                }
                if (qint64(result.matches.size()) > remaining) {
                    result.matches.resize(size_t(remaining));
                    result.truncated = true;
                    stats.limitReached = true;
                    stop = true;
                }
                ++stats.filesMatched;
                stats.matchCount += result.matchCount();
                outbox.push_back(std::move(result));
                break;
            }
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(size_t(threads));
    for (int i = 0; i < threads; ++i) pool.emplace_back(worker);

    qint64 lastFlush = 0;
    auto flush = [&](bool force) {
        if (!force && clock.elapsed() - lastFlush < 100) return;
        lastFlush = clock.elapsed();
        std::vector<FileResult> batch;
        SearchStats snapshot;
        {
            std::lock_guard lock(mutex);
            batch.swap(outbox);
            snapshot = stats;
        }
        snapshot.durationSeconds = clock.elapsed() / 1000.0;
        onBatch(std::move(batch), snapshot);
    };

    FileWalker(query).walk(
        [&](const FileCandidate &candidate) {
            {
                std::unique_lock lock(mutex);
                // Bounded queue: the walker waits instead of piling up paths in memory.
                while (queue.size() >= queueCapacity && !stopped()) {
                    queueChanged.wait_for(lock, std::chrono::milliseconds(50));
                    if (clock.elapsed() - lastFlush >= 100) {
                        lock.unlock();
                        flush(false);
                        lock.lock();
                    }
                }
                if (stopped()) return false;
                queue.push_back(candidate);
            }
            queueChanged.notify_one();
            flush(false);
            return true;
        },
        cancel);

    {
        std::lock_guard lock(mutex);
        producerDone = true;
    }
    queueChanged.notify_all();

    // Keep delivering results while the workers finish the queue.
    for (;;) {
        bool idle = false;
        {
            std::unique_lock lock(mutex);
            queueChanged.wait_for(lock, std::chrono::milliseconds(50));
            idle = queue.empty() || stopped();
        }
        flush(false);
        if (idle) break;
    }
    queueChanged.notify_all();
    for (std::thread &thread : pool) thread.join();
    flush(true);

    std::lock_guard lock(mutex);
    stats.durationSeconds = clock.elapsed() / 1000.0;
    return stats;
}

} // namespace garimpo
