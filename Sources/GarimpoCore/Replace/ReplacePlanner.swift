import Foundation

public struct LineChange: Codable, Hashable, Identifiable, Sendable {
    public var id = UUID()
    public var lineNumber: Int
    public var original: String
    public var replaced: String
    public var included = true
}

public struct FileChange: Codable, Hashable, Identifiable, Sendable {
    public var id = UUID()
    public var path: String
    public var encoding: String
    public var lines: [LineChange]
    public var included = true

    public var name: String { (path as NSString).lastPathComponent }
    public var selectedLineCount: Int { included ? lines.filter(\.included).count : 0 }
}

public struct ReplacePlan: Sendable {
    public var query: SearchQuery
    public var replacement: String
    public var files: [FileChange]
    /// Files that could not be planned, with the reason.
    public var skipped: [(path: String, reason: String)]

    public var selectedLineCount: Int { files.reduce(0) { $0 + $1.selectedLineCount } }
    public var selectedFileCount: Int { files.filter { $0.selectedLineCount > 0 }.count }
}

public enum ReplacePlanner {
    /// Builds a preview of every line that would change. Nothing is written.
    public static func plan(query: SearchQuery, replacement: String, paths: [String]) throws -> ReplacePlan {
        let regex = try Matcher(query: query).replacementRegex()
        let template = query.mode == .literal ? NSRegularExpression.escapedTemplate(for: replacement) : replacement

        var plan = ReplacePlan(query: query, replacement: replacement, files: [], skipped: [])
        for path in paths {
            let url = URL(fileURLWithPath: path)
            if DocumentExtractor.canExtract(url), query.searchDocuments {
                plan.skipped.append((path, "Documents are read-only"))
                continue
            }
            let decoded: DecodedText
            do {
                decoded = try TextDecoder.decode(contentsOf: url)
            } catch {
                plan.skipped.append((path, "Not a readable text file"))
                continue
            }
            var changes: [LineChange] = []
            for (index, line) in TextLines.split(decoded.text).enumerated() {
                let replaced = apply(regex, template: template, to: line.content)
                if replaced != line.content {
                    changes.append(LineChange(lineNumber: index + 1, original: line.content, replaced: replaced))
                }
            }
            if !changes.isEmpty {
                plan.files.append(FileChange(path: path, encoding: decoded.encodingName, lines: changes))
            }
        }
        return plan
    }

    static func apply(_ regex: NSRegularExpression, template: String, to text: String) -> String {
        regex.stringByReplacingMatches(in: text, range: NSRange(text.startIndex..., in: text), withTemplate: template)
    }
}
