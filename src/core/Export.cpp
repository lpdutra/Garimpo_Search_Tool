#include "Export.h"

#include <QLocale>

namespace garimpo {

namespace {

QString quoteCsv(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

} // namespace

QString Exporter::extension(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Text: return QStringLiteral("txt");
    case ExportFormat::Csv: return QStringLiteral("csv");
    case ExportFormat::Html: return QStringLiteral("html");
    }
    return {};
}

QString Exporter::title(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Text: return QStringLiteral("Plain Text");
    case ExportFormat::Csv: return QStringLiteral("CSV");
    case ExportFormat::Html: return QStringLiteral("HTML");
    }
    return {};
}

QString Exporter::render(const ResultDocument &document, ExportFormat format)
{
    switch (format) {
    case ExportFormat::Text: return text(document);
    case ExportFormat::Csv: return csv(document);
    case ExportFormat::Html: return html(document);
    }
    return {};
}

QString Exporter::text(const ResultDocument &document)
{
    QString output;
    for (const FileResult &file : document.files) {
        if (file.matches.empty()) {
            output += file.path + QLatin1Char('\n');
            continue;
        }
        for (const LineMatch &match : file.matches)
            output += QStringLiteral("%1:%2:%3: %4\n").arg(file.path).arg(match.lineNumber).arg(match.column()).arg(match.text);
    }
    return output;
}

QString Exporter::csv(const ResultDocument &document)
{
    QString output = QStringLiteral("path,line,column,text\n");
    for (const FileResult &file : document.files) {
        if (file.matches.empty()) output += quoteCsv(file.path) + QStringLiteral(",,,\n");
        for (const LineMatch &match : file.matches)
            output += QStringLiteral("%1,%2,%3,%4\n").arg(quoteCsv(file.path)).arg(match.lineNumber).arg(match.column()).arg(quoteCsv(match.text));
    }
    return output;
}

QString Exporter::highlightedHtml(const LineMatch &match)
{
    QString output;
    qsizetype cursor = 0;
    const QString &text = match.text;
    for (const Span &span : match.spans) {
        if (span.start < cursor || span.start + span.length > text.size()) continue;
        output += text.mid(cursor, span.start - cursor).toHtmlEscaped();
        output += QStringLiteral("<mark>") + text.mid(span.start, span.length).toHtmlEscaped() + QStringLiteral("</mark>");
        cursor = span.start + span.length;
    }
    output += text.mid(cursor).toHtmlEscaped();
    return output;
}

QString Exporter::html(const ResultDocument &document)
{
    qint64 total = 0;
    for (const FileResult &file : document.files) total += file.matchCount();
    QString body = QStringLiteral("<h1>%1</h1>\n<p class=\"meta\">Pattern: <code>%2</code> &middot; Folders: <code>%3</code> &middot; "
                                  "%4 files, %5 matches &middot; %6</p>\n")
                       .arg(document.title.toHtmlEscaped(), document.query.pattern.toHtmlEscaped(),
                            document.query.directories.toHtmlEscaped())
                       .arg(document.files.size())
                       .arg(total)
                       .arg(QLocale().toString(document.createdAt, QLocale::ShortFormat));
    for (const FileResult &file : document.files) {
        body += QStringLiteral("<h2>%1</h2>\n").arg(file.path.toHtmlEscaped());
        if (file.matches.empty()) continue;
        body += QStringLiteral("<table>\n");
        for (const LineMatch &match : file.matches)
            body += QStringLiteral("<tr><td class=\"line\">%1</td><td><pre>%2</pre></td></tr>\n").arg(match.lineNumber).arg(highlightedHtml(match));
        body += QStringLiteral("</table>\n");
    }
    return QStringLiteral(
               "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>%1</title>\n<style>\n"
               "body { font: 13px -apple-system, 'Segoe UI', Ubuntu, sans-serif; margin: 24px; color: #222; }\n"
               "h1 { font-size: 20px; } h2 { font-size: 13px; margin: 18px 0 4px; color: #555; }\n"
               ".meta { color: #666; } table { border-collapse: collapse; width: 100%; }\n"
               "td { vertical-align: top; padding: 1px 6px; } td.line { color: #888; text-align: right; width: 1%; }\n"
               "pre { margin: 0; font: 12px ui-monospace, Menlo, 'DejaVu Sans Mono', monospace; white-space: pre-wrap; }\n"
               "mark { background: #ffe066; }\n</style></head><body>\n%2</body></html>\n")
        .arg(document.title.toHtmlEscaped(), body);
}

} // namespace garimpo
