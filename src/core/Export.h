#pragma once

#include "Storage.h"

#include <QString>

namespace garimpo {

enum class ExportFormat { Text, Csv, Html };

class Exporter {
public:
    static QString extension(ExportFormat format);
    static QString title(ExportFormat format);
    static QString render(const ResultDocument &document, ExportFormat format);

    /// `path:line:column: text`, the layout grep and most editors understand.
    static QString text(const ResultDocument &document);
    static QString csv(const ResultDocument &document);
    static QString html(const ResultDocument &document);
    static QString highlightedHtml(const LineMatch &match);
};

} // namespace garimpo
