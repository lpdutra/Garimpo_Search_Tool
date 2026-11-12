#pragma once

#include "SearchEngine.h"
#include "Storage.h"

#include <QObject>
#include <QPointer>
#include <QThread>

#include <atomic>
#include <memory>
#include <vector>

/// One search and its results, shown as a tab.
class SearchTab : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Running, Finished, Cancelled, Failed };

    explicit SearchTab(const garimpo::SearchQuery &query, QObject *parent = nullptr);
    SearchTab(garimpo::ResultDocument document, const QString &filePath, QObject *parent = nullptr);
    ~SearchTab() override;

    const garimpo::SearchQuery &query() const { return m_query; }
    void setQuery(const garimpo::SearchQuery &query);
    const std::vector<garimpo::FileResult> &files() const { return m_files; }
    const garimpo::SearchStats &stats() const { return m_stats; }
    State state() const { return m_state; }
    bool isRunning() const { return m_state == State::Running; }
    QString errorMessage() const { return m_error; }
    QDateTime createdAt() const { return m_createdAt; }

    QString title() const { return m_title; }
    bool hasCustomTitle() const { return m_customTitle; }
    void setTitle(const QString &title);

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path) { m_filePath = path; }
    bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    /// Selected result row in the list, kept per tab.
    int selectedRow = -1;

    void start(const garimpo::SearchLimits &limits);
    void stop();
    void removeFile(int index);

    garimpo::ResultDocument document() const;
    static QString defaultTitle(const garimpo::SearchQuery &query);

Q_SIGNALS:
    void filesAppended(int first, int count);
    void resultsReset();
    void stateChanged();
    void titleChanged();

private:
    void finish(State state, const garimpo::SearchStats &stats, const QString &error);

    garimpo::SearchQuery m_query;
    std::vector<garimpo::FileResult> m_files;
    garimpo::SearchStats m_stats;
    State m_state = State::Idle;
    QString m_error;
    QString m_title;
    bool m_customTitle = false;
    QString m_filePath;
    bool m_dirty = false;
    QDateTime m_createdAt = QDateTime::currentDateTime();

    std::shared_ptr<std::atomic<bool>> m_cancel;
    QPointer<QThread> m_thread;
    quint64 m_generation = 0;
};
