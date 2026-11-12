import GarimpoCore
import SwiftUI

enum ResultSort: String, CaseIterable {
    case found = "Search Order"
    case path = "Path"
    case matches = "Match Count"
    case modified = "Date Modified"
}

struct ResultsView: View {
    @Bindable var app: AppState
    @Bindable var tab: SearchTab
    @State private var sort: ResultSort = .found

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Image(systemName: "line.3.horizontal.decrease").foregroundStyle(.secondary)
                TextField("Filter results by path", text: $app.resultFilter)
                    .textFieldStyle(.plain)
                Picker("Sort", selection: $sort) {
                    ForEach(ResultSort.allCases, id: \.self) { Text($0.rawValue).tag($0) }
                }
                .labelsHidden()
                .fixedSize()
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 6)
            Divider()

            if tab.files.isEmpty {
                emptyState
            } else {
                list
            }
        }
    }

    private var visibleFiles: [FileResult] {
        var files = tab.files
        let filter = app.resultFilter.trimmingCharacters(in: .whitespaces)
        if !filter.isEmpty {
            files = files.filter { $0.path.localizedCaseInsensitiveContains(filter) }
        }
        switch sort {
        case .found: break
        case .path: files.sort { $0.path.localizedStandardCompare($1.path) == .orderedAscending }
        case .matches: files.sort { $0.matchCount > $1.matchCount }
        case .modified: files.sort { ($0.modified ?? .distantPast) > ($1.modified ?? .distantPast) }
        }
        return files
    }

    private var list: some View {
        ScrollViewReader { proxy in
            List(selection: $tab.selection) {
                ForEach(visibleFiles) { file in
                    FileHeader(file: file, roots: tab.query.directoryList)
                        .tag(ResultSelection(fileID: file.id, matchID: nil))
                        .id(ResultSelection(fileID: file.id, matchID: nil))
                    ForEach(file.matches) { match in
                        MatchRow(match: match)
                            .tag(ResultSelection(fileID: file.id, matchID: match.id))
                            .id(ResultSelection(fileID: file.id, matchID: match.id))
                    }
                    if file.truncated {
                        Text("More matches not shown")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .padding(.leading, 52)
                            .selectionDisabled()
                    }
                }
            }
            .listStyle(.inset)
            .contextMenu(forSelectionType: ResultSelection.self) { selections in
                contextMenu(for: selections)
            } primaryAction: { selections in
                if let first = selections.first { tab.selection = first }
                app.openSelection(in: tab)
            }
            .onChange(of: tab.selection) { _, selection in
                guard let selection else { return }
                proxy.scrollTo(selection)
            }
        }
    }

    @ViewBuilder
    private var emptyState: some View {
        VStack(spacing: 8) {
            Spacer()
            switch tab.state {
            case .running:
                ProgressView()
                Text("Searching…").foregroundStyle(.secondary)
            case .failed(let message):
                Image(systemName: "exclamationmark.triangle").font(.largeTitle).foregroundStyle(.orange)
                Text(message).foregroundStyle(.secondary).multilineTextAlignment(.center)
            default:
                Image(systemName: "magnifyingglass").font(.largeTitle).foregroundStyle(.tertiary)
                Text("No matches").foregroundStyle(.secondary)
            }
            Spacer()
        }
        .frame(maxWidth: .infinity)
        .padding()
    }

    @ViewBuilder
    private func contextMenu(for selections: Set<ResultSelection>) -> some View {
        let files = selections.compactMap { selection in tab.files.first { $0.id == selection.fileID } }
        let paths = Array(Set(files.map(\.path))).sorted()
        if let selection = selections.first, selections.count == 1 {
            Button("Open in Editor") {
                tab.selection = selection
                app.openSelection(in: tab)
            }
        }
        Button("Open with Default App") { paths.forEach(FileActions.openWithDefaultApp) }
        Button("Show in Finder") { FileActions.revealInFinder(paths: paths) }
        Divider()
        Button("Copy Path") { FileActions.copy(paths.joined(separator: "\n")) }
        Button("Copy Name") { FileActions.copy(files.map(\.name).joined(separator: "\n")) }
        let lines = selections.compactMap { selection -> String? in
            guard let file = tab.files.first(where: { $0.id == selection.fileID }),
                  let match = file.matches.first(where: { $0.id == selection.matchID }) else { return nil }
            return match.text
        }
        if !lines.isEmpty {
            Button("Copy Line") { FileActions.copy(lines.joined(separator: "\n")) }
        }
        Divider()
        Button("Remove from Results") {
            Set(files.map(\.id)).forEach(tab.removeFile)
        }
    }
}

private struct FileHeader: View {
    let file: FileResult
    let roots: [String]

    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: file.kind == .document ? "doc.richtext" : "doc.text")
                .foregroundStyle(.secondary)
            HighlightedText(text: file.name, spans: file.nameSpans, font: .body.weight(.semibold))
            Text(relativeDirectory)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)
            Spacer(minLength: 4)
            if file.matchCount > 0 {
                Text(file.matchCount.formatted())
                    .font(.caption.monospacedDigit())
                    .padding(.horizontal, 6)
                    .padding(.vertical, 1)
                    .background(Capsule().fill(Color.accentColor.opacity(0.2)))
            }
        }
        .contentShape(Rectangle())
        .help(file.path)
    }

    private var relativeDirectory: String {
        let directory = file.directory
        for root in roots where directory.hasPrefix(root) {
            let relative = String(directory.dropFirst(root.count)).trimmingCharacters(in: CharacterSet(charactersIn: "/"))
            return relative.isEmpty ? "" : relative
        }
        return (directory as NSString).abbreviatingWithTildeInPath
    }
}

private struct MatchRow: View {
    let match: LineMatch

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            Text(String(match.lineNumber))
                .font(.system(.caption, design: .monospaced))
                .foregroundStyle(.secondary)
                .frame(minWidth: 44, alignment: .trailing)
            HighlightedText(text: match.text, spans: match.spans, trimLeading: true,
                            font: .system(.body, design: .monospaced))
                .lineLimit(1)
                .truncationMode(.tail)
        }
    }
}

/// Text with the given UTF-16 spans painted as highlights.
struct HighlightedText: View {
    let text: String
    let spans: [TextSpan]
    var trimLeading = false
    var font: Font = .body

    var body: some View {
        Text(attributed).font(font)
    }

    private var attributed: AttributedString {
        let source = text as NSString
        var offset = 0
        if trimLeading {
            while offset < source.length, let scalar = UnicodeScalar(source.character(at: offset)),
                  CharacterSet.whitespaces.contains(scalar) {
                offset += 1
            }
        }
        let shown = source.substring(from: offset)
        var result = AttributedString(shown)
        for span in spans {
            let location = span.location - offset
            guard location >= 0, location + span.length <= (shown as NSString).length,
                  let range = Range(NSRange(location: location, length: span.length), in: shown),
                  let lower = AttributedString.Index(range.lowerBound, within: result),
                  let upper = AttributedString.Index(range.upperBound, within: result) else { continue }
            result[lower..<upper].backgroundColor = Color.yellow.opacity(0.55)
            result[lower..<upper].foregroundColor = Color.black
        }
        return result
    }
}
