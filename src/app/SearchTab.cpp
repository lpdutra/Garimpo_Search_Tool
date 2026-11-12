#include "SearchTab.h"

using namespace garimpo;

SearchTab::SearchTab(const SearchQuery &query, QObject *parent)
    : QObject(parent), m_query(query), m_title(defaultTitle(query))
{
}

SearchTab::SearchTab(ResultDocument document, const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_query(document.query)
    , m_files(std::move(document.files))
    , m_stats(document.stats)
    , m_state(State::Finished)
    , m_title(document.title)
    , m_filePath(filePath)
    , m_createdAt(document.createdAt)
{
    m_customTitle = m_title != defaultTitle(m_query);
    if (!m_files.empty()) selectedRow = m_files.front().matches.empty() ? 0 : 1;
}

SearchTab::~SearchTab()
{
    stop();
    if (m_thread) m_thread->wait();
}

QString SearchTab::defaultTitle(const SearchQuery &query)
{
    const QString summary = query.summary();
    return summary.size() > 40 ? summary.left(39) + QChar(0x2026) : summary;
}

void SearchTab::setQuery(const SearchQuery &query)
{
    m_query = query;
    if (!m_customTitle) {
        m_title = defaultTitle(query);
        Q_EMIT titleChanged();
    }
}

void SearchTab::setTitle(const QString &title)
{
    const QString trimmed = title.trimmed();
    m_customTitle = !trimmed.isEmpty();
    m_title = trimmed.isEmpty() ? defaultTitle(m_query) : trimmed;
    m_dirty = true;
    Q_EMIT titleChanged();
}

void SearchTab::start(const SearchLimits &limits)
{
    stop();
    m_files.clear();
    m_files.shrink_to_fit();
    m_stats = {};
    m_error.clear();
    m_state = State::Running;
    m_createdAt = QDateTime::currentDateTime();
    m_dirty = true;
    selectedRow = -1;
    Q_EMIT resultsReset();
    Q_EMIT stateChanged();

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_cancel = cancel;
    const quint64 generation = ++m_generation;
    const SearchQuery query = m_query;
    QPointer<SearchTab> self(this);

    // Results are handed to the UI thread in batches; a newer search makes older batches stale.
    auto deliver = [self, generation](std::vector<FileResult> &&files, const SearchStats &stats) {
        auto shared = std::make_shared<std::vector<FileResult>>(std::move(files));
        QMetaObject::invokeMethod(self.data(), [self, generation, shared, stats] {
            if (!self || self->m_generation != generation) return;
            self->m_stats = stats;
            if (!shared->empty()) {
                const int first = int(self->m_files.size());
                for (FileResult &file : *shared) self->m_files.push_back(std::move(file));
                Q_EMIT self->filesAppended(first, int(shared->size()));
            }
            Q_EMIT self->stateChanged();
        }, Qt::QueuedConnection);
    };

    QThread *thread = QThread::create([self, generation, query, limits, cancel, deliver] {
        SearchStats stats;
        QString error;
        try {
            stats = SearchEngine::run(query, limits, cancel.get(), &ExtractionCache::shared(), deliver);
        } catch (const PatternError &problem) {
            error = problem.message();
        }
        const bool cancelled = cancel->load();
        QMetaObject::invokeMethod(self.data(), [self, generation, stats, error, cancelled] {
            if (!self || self->m_generation != generation) return;
            self->finish(!error.isEmpty() ? State::Failed : (cancelled ? State::Cancelled : State::Finished), stats, error);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_thread = thread;
    thread->start();
}

void SearchTab::finish(State state, const SearchStats &stats, const QString &error)
{
    m_state = state;
    m_error = error;
    if (state != State::Failed) m_stats = stats;
    Q_EMIT stateChanged();
}

void SearchTab::stop()
{
    if (m_cancel) m_cancel->store(true);
    if (m_state == State::Running) {
        m_state = State::Cancelled;
        Q_EMIT stateChanged();
    }
}

void SearchTab::removeFile(int index)
{
    if (index < 0 || index >= int(m_files.size())) return;
    m_files.erase(m_files.begin() + index);
    m_dirty = true;
    Q_EMIT resultsReset();
}

ResultDocument SearchTab::document() const
{
    ResultDocument document;
    document.title = m_title;
    document.query = m_query;
    document.createdAt = m_createdAt;
    document.files = m_files;
    document.stats = m_stats;
    return document;
}
