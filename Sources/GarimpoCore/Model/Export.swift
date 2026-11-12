import Foundation

public enum ExportFormat: String, CaseIterable, Sendable {
    case text = "txt"
    case csv
    case html

    public var title: String {
        switch self {
        case .text: "Plain Text"
        case .csv: "CSV"
        case .html: "HTML"
        }
    }
}

public enum ResultExporter {
    public static func export(_ document: ResultDocument, as format: ExportFormat) -> String {
        switch format {
        case .text: text(document)
        case .csv: csv(document)
        case .html: html(document)
        }
    }

    /// `path:line:column: text`, the same layout grep and most editors understand.
    public static func text(_ document: ResultDocument) -> String {
        var output = ""
        for file in document.files {
            if file.matches.isEmpty {
                output += file.path + "\n"
                continue
            }
            for match in file.matches {
                output += "\(file.path):\(match.lineNumber):\(match.column): \(match.text)\n"
            }
        }
        return output
    }

    public static func csv(_ document: ResultDocument) -> String {
        var output = "path,line,column,text\n"
        for file in document.files {
            if file.matches.isEmpty {
                output += "\(quote(file.path)),,,\n"
            }
            for match in file.matches {
                output += "\(quote(file.path)),\(match.lineNumber),\(match.column),\(quote(match.text))\n"
            }
        }
        return output
    }

    public static func html(_ document: ResultDocument) -> String {
        let total = document.files.reduce(0) { $0 + $1.matchCount }
        var body = """
        <h1>\(escape(document.title))</h1>
        <p class="meta">Pattern: <code>\(escape(document.query.pattern))</code> &middot; \
        Folders: <code>\(escape(document.query.directories))</code> &middot; \
        \(document.files.count) files, \(total) matches &middot; \
        \(document.createdAt.formatted(date: .abbreviated, time: .shortened))</p>

        """
        for file in document.files {
            body += "<h2>\(escape(file.path))</h2>\n"
            guard !file.matches.isEmpty else { continue }
            body += "<table>\n"
            for match in file.matches {
                body += "<tr><td class=\"line\">\(match.lineNumber)</td><td><pre>\(highlighted(match))</pre></td></tr>\n"
            }
            body += "</table>\n"
        }
        return """
        <!DOCTYPE html>
        <html><head><meta charset="utf-8"><title>\(escape(document.title))</title>
        <style>
        body { font: 13px -apple-system, sans-serif; margin: 24px; color: #222; }
        h1 { font-size: 20px; } h2 { font-size: 13px; margin: 18px 0 4px; color: #555; }
        .meta { color: #666; } table { border-collapse: collapse; width: 100%; }
        td { vertical-align: top; padding: 1px 6px; } td.line { color: #888; text-align: right; width: 1%; }
        pre { margin: 0; font: 12px ui-monospace, Menlo, monospace; white-space: pre-wrap; }
        mark { background: #ffe066; }
        </style></head><body>
        \(body)</body></html>
        """
    }

    static func highlighted(_ match: LineMatch) -> String {
        let text = match.text as NSString
        var output = ""
        var cursor = 0
        for span in match.spans where span.location >= cursor && span.location + span.length <= text.length {
            output += escape(text.substring(with: NSRange(location: cursor, length: span.location - cursor)))
            output += "<mark>" + escape(text.substring(with: span.nsRange)) + "</mark>"
            cursor = span.location + span.length
        }
        output += escape(text.substring(from: cursor))
        return output
    }

    static func escape(_ value: String) -> String {
        value
            .replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
            .replacingOccurrences(of: "\"", with: "&quot;")
    }

    static func quote(_ value: String) -> String {
        "\"" + value.replacingOccurrences(of: "\"", with: "\"\"") + "\""
    }
}
