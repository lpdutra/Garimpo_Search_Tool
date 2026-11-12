#pragma once

#include "FileWalker.h"

#include <QMutex>
#include <QString>

#include <optional>

namespace garimpo {

/// Pulls plain text out of document formats.
class DocumentExtractor {
public:
    static bool canExtract(const QString &path);
    static std::optional<QString> extractText(const QString &path);

    /// Path of the `pdftotext` tool (from poppler), or empty when it is not installed.
    static QString pdfToTextPath();

    // Exposed for tests.
    static std::optional<QByteArray> readZipEntry(const QByteArray &zip, const QString &entryName);
    static QString textFromWordXml(const QByteArray &xml);
    static QString textFromOpenDocumentXml(const QByteArray &xml);
    static QString textFromRtf(const QByteArray &rtf);
};

/// Disk cache for extracted document text, keyed by path and invalidated by size and modification time.
class ExtractionCache {
public:
    explicit ExtractionCache(QString directory);
    static ExtractionCache &shared();

    std::optional<QString> text(const FileCandidate &candidate);

    struct CleanupReport {
        int removed = 0;
        int kept = 0;
    };
    CleanupReport removeStale();
    void clear();
    qint64 sizeOnDisk() const;

private:
    QString entryPath(const QString &sourcePath) const;

    QString m_directory;
    QMutex m_mutex;
};

} // namespace garimpo
