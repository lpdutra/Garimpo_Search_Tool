import Foundation
import GarimpoCore
import Observation

struct ResultSelection: Hashable {
    var fileID: UUID
    var matchID: UUID?
}

@MainActor
@Observable
final class SearchTab: Identifiable {
    enum State: Equatable {
        case idle
        case running
        case finished
        case cancelled
        case failed(String)
    }

    let id = UUID()
    var title: String
    var customTitle = false
    var query: SearchQuery
    var files: [FileResult] = []
    var stats = SearchStats()
    var state: State = .idle
    var createdAt = Date()
    var selection: ResultSelection?
    var fileURL: URL?
    var isDirty = false

    @ObservationIgnored var task: Task<Void, Never>?

    init(query: SearchQuery) {
        self.query = query
        self.title = Self.defaultTitle(for: query)
    }

    init(document: ResultDocument, fileURL: URL? = nil) {
        query = document.query
        title = document.title
        customTitle = document.title != Self.defaultTitle(for: document.query)
        files = document.files
        stats = document.stats ?? SearchStats()
        createdAt = document.createdAt
        state = .finished
        self.fileURL = fileURL
        if let first = files.first {
            selection = ResultSelection(fileID: first.id, matchID: first.matches.first?.id)
        }
    }

    static func defaultTitle(for query: SearchQuery) -> String {
        let summary = query.summary
        return summary.count > 40 ? String(summary.prefix(39)) + "…" : summary
    }

    var isRunning: Bool { state == .running }

    var document: ResultDocument {
        ResultDocument(title: title, query: query, createdAt: createdAt, files: files, stats: stats)
    }

    var selectedFile: FileResult? {
        guard let selection else { return nil }
        return files.first { $0.id == selection.fileID }
    }

    var selectedMatch: LineMatch? {
        guard let matchID = selection?.matchID else { return nil }
        return selectedFile?.matches.first { $0.id == matchID }
    }

    /// Moves the selection by `step` matches, crossing file boundaries.
    func selectMatch(offset step: Int) {
        let flat = files.flatMap { file in
            file.matches.isEmpty
                ? [ResultSelection(fileID: file.id, matchID: nil)]
                : file.matches.map { ResultSelection(fileID: file.id, matchID: $0.id) }
        }
        guard !flat.isEmpty else { return }
        guard let selection, let index = flat.firstIndex(of: selection) ?? flat.firstIndex(where: { $0.fileID == selection.fileID }) else {
            self.selection = step >= 0 ? flat.first : flat.last
            return
        }
        self.selection = flat[(index + step + flat.count) % flat.count]
    }

    /// Moves the selection to the first match of the next or previous file.
    func selectFile(offset step: Int) {
        guard !files.isEmpty else { return }
        let current = selection.flatMap { selection in files.firstIndex { $0.id == selection.fileID } }
        let index = current.map { ($0 + step + files.count) % files.count } ?? (step >= 0 ? 0 : files.count - 1)
        let file = files[index]
        selection = ResultSelection(fileID: file.id, matchID: file.matches.first?.id)
    }

    func removeFile(_ fileID: UUID) {
        files.removeAll { $0.id == fileID }
        if selection?.fileID == fileID { selection = nil }
        isDirty = true
    }
}
