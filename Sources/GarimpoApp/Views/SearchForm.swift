import GarimpoCore
import SwiftUI
import UniformTypeIdentifiers

struct SearchForm: View {
    @Bindable var app: AppState
    @State private var showFilters = false
    @State private var dropTargeted = false

    private let labelWidth: CGFloat = 64

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Grid(alignment: .leading, horizontalSpacing: 8, verticalSpacing: 8) {
                GridRow {
                    label("Find")
                    HistoryComboBox(text: $app.form.pattern, items: app.history.values(.patterns),
                                    placeholder: patternPlaceholder, monospaced: true,
                                    focusToken: app.focusPatternToken, onSubmit: app.search)
                    Picker("Mode", selection: $app.form.mode) {
                        ForEach(PatternMode.allCases, id: \.self) { Text($0.title).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .fixedSize()
                    .help("Text matches literally, Regex uses regular expressions, Expression combines terms with AND, OR, NOT")
                }
                GridRow {
                    label("Replace")
                    HistoryComboBox(text: $app.replacement, items: app.history.values(.replacements),
                                    placeholder: app.form.mode == .regex ? "Replacement ($1, $2 for groups)" : "Replacement",
                                    monospaced: true, onSubmit: app.beginReplace)
                    Button("Replace…", action: app.beginReplace)
                        .disabled(app.form.mode == .expression || app.form.fileNamesOnly)
                }
                GridRow {
                    label("Folders")
                    HistoryComboBox(text: $app.form.directories, items: app.history.values(.directories),
                                    placeholder: "Folder to search — drop folders here; separate several with ;",
                                    onSubmit: app.search)
                    FolderMenu(app: app)
                }
                GridRow {
                    label("Files")
                    HStack(spacing: 8) {
                        HistoryComboBox(text: $app.form.includeMasks, items: app.history.values(.includeMasks),
                                        placeholder: "*.swift;*.md (empty = all)", onSubmit: app.search)
                        Text("Exclude").foregroundStyle(.secondary)
                        HistoryComboBox(text: $app.form.excludeMasks, items: app.history.values(.excludeMasks),
                                        placeholder: "*.min.js;*.log", onSubmit: app.search)
                    }
                    Color.clear.frame(width: 1, height: 1)
                }
            }

            HStack(spacing: 14) {
                Toggle("Match case", isOn: $app.form.caseSensitive)
                Toggle("Whole word", isOn: $app.form.wholeWord)
                Toggle("Subfolders", isOn: $app.form.recursive)
                Toggle("Hidden files", isOn: $app.form.includeHidden)
                Toggle("Documents", isOn: $app.form.searchDocuments)
                    .help("Also search inside PDF, RTF, DOC, DOCX and ODT files")
                Toggle("File names only", isOn: $app.form.fileNamesOnly)
                if app.form.mode == .expression {
                    Picker("Terms on", selection: $app.form.expressionScope) {
                        ForEach(ExpressionScope.allCases, id: \.self) { Text($0.title).tag($0) }
                    }
                    .fixedSize()
                }
                Spacer()
                Button {
                    withAnimation(.easeInOut(duration: 0.15)) { showFilters.toggle() }
                } label: {
                    Label("Filters", systemImage: showFilters ? "chevron.up" : "line.3.horizontal.decrease.circle")
                }
                .buttonStyle(.borderless)
                if app.selectedTab?.isRunning == true {
                    Button("Stop") { app.stop() }
                        .keyboardShortcut(.cancelAction)
                }
                Button("Search", action: app.search)
                    .keyboardShortcut(.return, modifiers: .command)
                    .buttonStyle(.borderedProminent)
            }
            .toggleStyle(.checkbox)

            if showFilters {
                FiltersView(query: $app.form)
                    .transition(.opacity)
            }
        }
        .padding(12)
        .background(dropTargeted ? Color.accentColor.opacity(0.08) : Color.clear)
        .onDrop(of: [.fileURL], isTargeted: $dropTargeted) { providers in
            loadURLs(from: providers)
            return true
        }
    }

    private var patternPlaceholder: String {
        switch app.form.mode {
        case .literal: "Text to find — leave empty to list files"
        case .regex: "Regular expression, e.g. func\\s+(\\w+)"
        case .expression: "e.g. invoice AND (paid OR \"due date\") NOT draft"
        }
    }

    private func label(_ text: String) -> some View {
        Text(text)
            .foregroundStyle(.secondary)
            .frame(width: labelWidth, alignment: .trailing)
    }

    private func loadURLs(from providers: [NSItemProvider]) {
        Task {
            var urls: [URL] = []
            for provider in providers {
                if let url = try? await provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier) as? Data,
                   let parsed = URL(dataRepresentation: url, relativeTo: nil) {
                    urls.append(parsed)
                }
            }
            app.useDroppedURLs(urls)
        }
    }
}

private struct FolderMenu: View {
    @Bindable var app: AppState

    var body: some View {
        Menu {
            Button("Choose Folder…", action: app.chooseFolders)
            let pinned = app.history.entries(.directories).filter(\.pinned)
            if !pinned.isEmpty {
                Section("Favorites") {
                    ForEach(pinned, id: \.value) { entry in
                        Button(entry.value) { app.form.directories = entry.value }
                    }
                }
            }
            Divider()
            let current = app.form.directories.trimmingCharacters(in: .whitespaces)
            if !current.isEmpty {
                if app.history.isPinned(current, in: .directories) {
                    Button("Remove from Favorites") {
                        app.history.setPinned(false, value: current, in: .directories)
                        app.saveHistory()
                    }
                } else {
                    Button("Add to Favorites") {
                        app.history.setPinned(true, value: current, in: .directories)
                        app.saveHistory()
                    }
                }
                Button("Remove from History") {
                    app.history.remove(current, from: .directories)
                    app.saveHistory()
                }
                Button("Show in Finder") {
                    FileActions.revealInFinder(paths: app.form.directoryList)
                }
            }
            Button("Clear Recent Folders") {
                app.history.clear(.directories)
                app.saveHistory()
            }
        } label: {
            Image(systemName: "folder")
        } primaryAction: {
            app.chooseFolders()
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
        .help("Click to choose a folder; hold for favorites and history")
    }
}

private struct FiltersView: View {
    @Binding var query: SearchQuery

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 8, verticalSpacing: 8) {
            GridRow {
                Text("Skip folders").foregroundStyle(.secondary).frame(width: 64 + 20, alignment: .trailing)
                TextField("Folder names to skip", text: $query.excludeDirectories)
                    .textFieldStyle(.roundedBorder)
                    .font(.system(.body, design: .monospaced))
            }
            GridRow {
                Text("Size (KB)").foregroundStyle(.secondary).frame(width: 64 + 20, alignment: .trailing)
                HStack {
                    TextField("min", value: $query.minSizeKB, format: .number).frame(width: 90)
                    Text("to").foregroundStyle(.secondary)
                    TextField("max", value: $query.maxSizeKB, format: .number).frame(width: 90)
                    Spacer()
                }
                .textFieldStyle(.roundedBorder)
            }
            GridRow {
                Text("Modified").foregroundStyle(.secondary).frame(width: 64 + 20, alignment: .trailing)
                HStack(spacing: 16) {
                    OptionalDatePicker(title: "after", date: $query.modifiedAfter)
                    OptionalDatePicker(title: "before", date: $query.modifiedBefore)
                    Spacer()
                    Button("Reset Filters") {
                        query.minSizeKB = nil
                        query.maxSizeKB = nil
                        query.modifiedAfter = nil
                        query.modifiedBefore = nil
                        query.excludeDirectories = SearchQuery().excludeDirectories
                    }
                }
            }
        }
    }
}

private struct OptionalDatePicker: View {
    let title: String
    @Binding var date: Date?

    var body: some View {
        HStack(spacing: 6) {
            Toggle(title, isOn: Binding(
                get: { date != nil },
                set: { date = $0 ? Calendar.current.startOfDay(for: Date()) : nil }
            ))
            .toggleStyle(.checkbox)
            if let current = date {
                DatePicker(title, selection: Binding(get: { current }, set: { date = $0 }), displayedComponents: .date)
                    .labelsHidden()
                    .datePickerStyle(.field)
            }
        }
    }
}
