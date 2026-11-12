import AppKit
import GarimpoCore
import Observation
import UniformTypeIdentifiers

extension UTType {
    static let garimpoResults = UTType(exportedAs: "com.lucasdutra.garimpo.results", conformingTo: .json)
}

struct AlertMessage: Identifiable {
    let id = UUID()
    var title: String
    var message: String
}

@MainActor
@Observable
final class AppState {
    var form: SearchQuery
    var replacement: String
    var history: History
    var tabs: [SearchTab] = []
    var selectedTabID: UUID?
    var alert: AlertMessage?
    var replaceModel: ReplaceModel?
    /// Bumped to move keyboard focus to the pattern field.
    var focusPatternToken = 0
    var resultFilter = ""

    init() {
        Preferences.registerDefaults()
        var loaded = History.load()
        loaded.limit = UserDefaults.standard.integer(forKey: Preferences.historyLimit)
        history = loaded
        form = loaded.lastQuery ?? SearchQuery()
        replacement = loaded.lastReplacement ?? ""

        if UserDefaults.standard.bool(forKey: Preferences.restoreSession), let session = Session.load() {
            tabs = session.tabs.map { SearchTab(document: $0) }
            if let index = session.selectedIndex, tabs.indices.contains(index) {
                selectedTabID = tabs[index].id
            } else {
                selectedTabID = tabs.last?.id
            }
        }
    }

    var selectedTab: SearchTab? {
        tabs.first { $0.id == selectedTabID }
    }

    // MARK: Searching

    func search() {
        guard validateForm() else { return }
        rememberForm()
        let tab: SearchTab
        if !UserDefaults.standard.bool(forKey: Preferences.openInNewTab), let current = selectedTab {
            current.task?.cancel()
            current.query = form
            if !current.customTitle { current.title = SearchTab.defaultTitle(for: form) }
            tab = current
        } else {
            tab = SearchTab(query: form)
            tabs.append(tab)
        }
        selectedTabID = tab.id
        run(tab)
    }

    func searchAgain(_ tab: SearchTab? = nil) {
        guard let tab = tab ?? selectedTab else { return }
        tab.task?.cancel()
        run(tab)
    }

    func stop(_ tab: SearchTab? = nil) {
        guard let tab = tab ?? selectedTab, tab.isRunning else { return }
        tab.task?.cancel()
        tab.state = .cancelled
    }

    private func run(_ tab: SearchTab) {
        tab.files = []
        tab.stats = SearchStats()
        tab.selection = nil
        tab.state = .running
        tab.createdAt = Date()
        tab.isDirty = true

        let query = tab.query
        let limits = Preferences.limits
        tab.task = Task { [weak self, weak tab] in
            do {
                for try await event in SearchEngine.run(query, limits: limits) {
                    guard let tab, !Task.isCancelled else { return }
                    switch event {
                    case .found(let files):
                        tab.files.append(contentsOf: files)
                        if tab.selection == nil, let first = tab.files.first {
                            tab.selection = ResultSelection(fileID: first.id, matchID: first.matches.first?.id)
                        }
                    case .progress(let stats):
                        tab.stats = stats
                    case .finished(let stats):
                        tab.stats = stats
                        tab.state = .finished
                    }
                }
                if let tab, tab.state == .running { tab.state = Task.isCancelled ? .cancelled : .finished }
            } catch {
                tab?.state = .failed(error.localizedDescription)
            }
            self?.saveSession()
        }
    }

    private func validateForm() -> Bool {
        if form.directoryList.isEmpty {
            alert = AlertMessage(title: "No folder", message: "Choose at least one folder to search.")
            return false
        }
        let missing = form.directoryList.filter { !FileManager.default.fileExists(atPath: $0) }
        if !missing.isEmpty {
            alert = AlertMessage(title: "Folder not found", message: missing.joined(separator: "\n"))
            return false
        }
        if !form.pattern.isEmpty {
            do {
                _ = try Matcher(query: form)
            } catch {
                alert = AlertMessage(title: "Invalid pattern", message: error.localizedDescription)
                return false
            }
        }
        return true
    }

    // MARK: History

    func rememberForm(includeReplacement: Bool = false) {
        history.remember(form, replacement: includeReplacement ? replacement : nil)
        history.lastReplacement = replacement
        saveHistory()
    }

    func saveHistory() {
        try? history.save()
    }

    func setHistoryLimit(_ limit: Int) {
        history.limit = limit
        for field in HistoryField.allCases {
            history.lists[field] = Array(history.entries(field).prefix(limit))
        }
        saveHistory()
    }

    func chooseFolders() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = true
        panel.prompt = "Choose"
        if let first = form.directoryList.first {
            panel.directoryURL = URL(fileURLWithPath: first)
        }
        guard panel.runModal() == .OK else { return }
        form.directories = panel.urls.map(\.path).joined(separator: ";")
    }

    func useDroppedURLs(_ urls: [URL]) {
        let folders = urls.map { url -> String in
            var isDirectory: ObjCBool = false
            FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory)
            return isDirectory.boolValue ? url.path : url.deletingLastPathComponent().path
        }
        guard !folders.isEmpty else { return }
        var unique: [String] = []
        for folder in folders where !unique.contains(folder) { unique.append(folder) }
        form.directories = unique.joined(separator: ";")
    }

    // MARK: Tabs

    func select(_ tab: SearchTab) {
        selectedTabID = tab.id
    }

    func close(_ tab: SearchTab) {
        tab.task?.cancel()
        guard let index = tabs.firstIndex(where: { $0.id == tab.id }) else { return }
        tabs.remove(at: index)
        if selectedTabID == tab.id {
            selectedTabID = tabs.isEmpty ? nil : tabs[min(index, tabs.count - 1)].id
        }
        saveSession()
    }

    func closeOthers(than tab: SearchTab) {
        for other in tabs where other.id != tab.id { other.task?.cancel() }
        tabs = [tab]
        selectedTabID = tab.id
        saveSession()
    }

    func closeSelectedTabOrWindow() {
        if let tab = selectedTab {
            close(tab)
        } else {
            NSApp.keyWindow?.performClose(nil)
        }
    }

    func move(_ tab: SearchTab, by offset: Int) {
        guard let index = tabs.firstIndex(where: { $0.id == tab.id }) else { return }
        let target = index + offset
        guard tabs.indices.contains(target) else { return }
        tabs.swapAt(index, target)
        saveSession()
    }

    func selectAdjacentTab(_ offset: Int) {
        guard !tabs.isEmpty else { return }
        let index = tabs.firstIndex { $0.id == selectedTabID } ?? 0
        selectedTabID = tabs[(index + offset + tabs.count) % tabs.count].id
    }

    func loadQueryIntoForm(_ tab: SearchTab) {
        form = tab.query
        focusPatternToken += 1
    }

    func rename(_ tab: SearchTab, to title: String) {
        let trimmed = title.trimmingCharacters(in: .whitespaces)
        tab.customTitle = !trimmed.isEmpty
        tab.title = trimmed.isEmpty ? SearchTab.defaultTitle(for: tab.query) : trimmed
        tab.isDirty = true
        saveSession()
    }

    // MARK: Session and documents

    func saveSession() {
        guard UserDefaults.standard.bool(forKey: Preferences.restoreSession) else {
            try? FileManager.default.removeItem(at: AppPaths.session)
            return
        }
        let finished = tabs.filter { !$0.isRunning }
        let selectedIndex = finished.firstIndex { $0.id == selectedTabID }
        try? Session(tabs: finished.map(\.document), selectedIndex: selectedIndex).save()
    }

    func openDocuments(_ urls: [URL]) {
        for url in urls {
            if let existing = tabs.first(where: { $0.fileURL == url }) {
                selectedTabID = existing.id
                continue
            }
            do {
                let tab = SearchTab(document: try ResultDocument.read(from: url), fileURL: url)
                tabs.append(tab)
                selectedTabID = tab.id
                NSDocumentController.shared.noteNewRecentDocumentURL(url)
            } catch {
                alert = AlertMessage(title: "Cannot open \(url.lastPathComponent)", message: error.localizedDescription)
            }
        }
        saveSession()
    }

    func showOpenPanel() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.garimpoResults]
        panel.allowsMultipleSelection = true
        guard panel.runModal() == .OK else { return }
        openDocuments(panel.urls)
    }

    func save(_ tab: SearchTab? = nil, saveAs: Bool = false) {
        guard let tab = tab ?? selectedTab else { return }
        var url = tab.fileURL
        if saveAs || url == nil {
            let panel = NSSavePanel()
            panel.allowedContentTypes = [.garimpoResults]
            panel.nameFieldStringValue = Self.safeFileName(tab.title) + "." + ResultDocument.fileExtension
            guard panel.runModal() == .OK, let chosen = panel.url else { return }
            url = chosen
        }
        guard let url else { return }
        do {
            try tab.document.write(to: url)
            tab.fileURL = url
            tab.isDirty = false
            NSDocumentController.shared.noteNewRecentDocumentURL(url)
        } catch {
            alert = AlertMessage(title: "Cannot save", message: error.localizedDescription)
        }
    }

    func export(_ format: ExportFormat) {
        guard let tab = selectedTab else { return }
        let panel = NSSavePanel()
        panel.allowedContentTypes = [UTType(filenameExtension: format.rawValue) ?? .plainText]
        panel.nameFieldStringValue = Self.safeFileName(tab.title) + "." + format.rawValue
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do {
            try ResultExporter.export(tab.document, as: format).write(to: url, atomically: true, encoding: .utf8)
        } catch {
            alert = AlertMessage(title: "Cannot export", message: error.localizedDescription)
        }
    }

    func print() {
        guard let tab = selectedTab else { return }
        ResultPrinter.print(tab.document)
    }

    static func safeFileName(_ title: String) -> String {
        let cleaned = title.components(separatedBy: CharacterSet(charactersIn: "/:\\?%*|\"<>")).joined(separator: "_")
        return cleaned.isEmpty ? "Results" : cleaned
    }

    // MARK: Replace

    func beginReplace() {
        guard validateForm() else { return }
        if form.pattern.isEmpty {
            alert = AlertMessage(title: "Nothing to replace", message: "Type the text to find first.")
            return
        }
        if form.mode == .expression || form.fileNamesOnly {
            alert = AlertMessage(title: "Replace not available",
                                 message: "Replace works with Text or Regex patterns on file contents.")
            return
        }
        rememberForm(includeReplacement: true)
        let model = ReplaceModel(query: form, replacement: replacement)
        replaceModel = model
        model.prepare()
    }

    func finishReplace(_ model: ReplaceModel) {
        guard let plan = model.plan else { return }
        let outcome = ReplaceExecutor.execute(plan)
        replaceModel = nil

        var message = ""
        if let batch = outcome.batch {
            message = "Changed \(batch.changedLines) lines in \(batch.entries.count) files.\nUse Search ▸ Undo Last Replace to revert."
        } else {
            message = "No file was changed."
        }
        if !outcome.failures.isEmpty {
            message += "\n\nNot changed:\n" + outcome.failures.map { "\($0.path): \($0.reason)" }.joined(separator: "\n")
        }
        alert = AlertMessage(title: "Replace finished", message: message)
        if let tab = selectedTab, tab.query.directories == plan.query.directories { searchAgain(tab) }
    }

    func undoLastReplace() {
        guard let batch = ReplaceExecutor.lastBatch() else {
            alert = AlertMessage(title: "Nothing to undo", message: "There is no replace to revert.")
            return
        }
        let confirm = NSAlert()
        confirm.messageText = "Undo the replace of “\(batch.pattern)” with “\(batch.replacement)”?"
        confirm.informativeText = "\(batch.entries.count) files changed on \(batch.date.formatted(date: .abbreviated, time: .standard)) will be restored from backup."
        confirm.addButton(withTitle: "Undo Replace")
        confirm.addButton(withTitle: "Cancel")
        guard confirm.runModal() == .alertFirstButtonReturn else { return }

        var outcome = ReplaceExecutor.undo(batch)
        if !outcome.modifiedSince.isEmpty {
            let force = NSAlert()
            force.messageText = "Some files were edited after the replace"
            force.informativeText = outcome.modifiedSince.joined(separator: "\n") + "\n\nRestoring them discards those edits."
            force.addButton(withTitle: "Keep Edits")
            force.addButton(withTitle: "Restore Anyway")
            if force.runModal() == .alertSecondButtonReturn {
                outcome = ReplaceExecutor.undo(batch, force: true)
            }
        }
        var message = "Restored \(outcome.restored.count) files."
        if !outcome.modifiedSince.isEmpty { message += "\nKept \(outcome.modifiedSince.count) edited files." }
        if !outcome.failures.isEmpty {
            message += "\n\nFailed:\n" + outcome.failures.map { "\($0.path): \($0.reason)" }.joined(separator: "\n")
        }
        alert = AlertMessage(title: "Undo finished", message: message)
        if let tab = selectedTab { searchAgain(tab) }
    }

    // MARK: Results

    func openSelection(in tab: SearchTab? = nil) {
        guard let tab = tab ?? selectedTab, let file = tab.selectedFile else { return }
        let match = tab.selectedMatch ?? (tab.selection?.matchID == nil ? nil : file.matches.first)
        if file.kind == .document {
            FileActions.openWithDefaultApp(path: file.path)
        } else {
            FileActions.openInEditor(path: file.path, line: match?.lineNumber, column: match?.column)
        }
    }

    func applicationWillTerminate() {
        for tab in tabs { tab.task?.cancel() }
        history.lastReplacement = replacement
        history.lastQuery = form
        saveHistory()
        saveSession()
    }
}
