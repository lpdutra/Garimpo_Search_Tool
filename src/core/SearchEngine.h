#pragma once

#include "Documents.h"
#include "Matcher.h"
#include "Query.h"
#include "Results.h"

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace garimpo {

struct SearchLimits {
    int maxLinesPerFile = 5000;
    qint64 maxTotalMatches = 200000;
    /// Larger plain-text files are skipped.
    qint64 maxFileSizeMB = 64;
    /// 0 picks a count from the number of cores, capped to keep memory low.
    int threads = 0;
};

class SearchEngine {
public:
    using BatchHandler = std::function<void(std::vector<FileResult> &&files, const SearchStats &stats)>;

    /// Runs a search, blocking the calling thread. `onBatch` is always called on the calling thread,
    /// with results grouped about every 100 ms. Throws PatternError for an invalid query.
    static SearchStats run(const SearchQuery &query, const SearchLimits &limits, const std::atomic<bool> *cancel,
                           ExtractionCache *cache, const BatchHandler &onBatch);

    enum class Outcome { Match, NoMatch, Binary, Unreadable, DocumentSkipped };

    static Outcome examine(const FileCandidate &candidate, const SearchQuery &query, const Matcher *matcher,
                           const SearchLimits &limits, ExtractionCache *cache, FileResult &result);
};

} // namespace garimpo
