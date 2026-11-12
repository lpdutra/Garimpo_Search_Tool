import GarimpoCore
import SwiftUI

struct SettingsView: View {
    @Bindable var app: AppState

    var body: some View {
        TabView {
            GeneralSettings(app: app)
                .tabItem { Label("General", systemImage: "gearshape") }
            HistorySettings(app: app)
                .tabItem { Label("History", systemImage: "clock.arrow.circlepath") }
            StorageSettings()
                .tabItem { Label("Storage", systemImage: "internaldrive") }
        }
        .frame(width: 620, height: 460)
    }
}

private struct GeneralSettings: View {
    let app: AppState
    @AppStorage(Preferences.editorCommand) private var editorCommand = Preferences.defaultEditorCommand
    @AppStorage(Preferences.openInNewTab) private var openInNewTab = true
    @AppStorage(Preferences.restoreSession) private var restoreSession = true
    @AppStorage(Preferences.historyLimit) private var historyLimit = History.defaultLimit
    @AppStorage(Preferences.maxFileSizeMB) private var maxFileSizeMB = 64
    @AppStorage(Preferences.maxMatchesPerFile) private var maxMatchesPerFile = 5_000
    @AppStorage(Preferences.maxTotalMatches) private var maxTotalMatches = 200_000

    var body: some View {
        Form {
            Section {
                TextField("Editor command", text: $editorCommand, prompt: Text("Empty opens files with their default app"))
                    .font(.system(.body, design: .monospaced))
                Text("Runs in your login shell. {file}, {line} and {column} are replaced. Examples: code -g {file}:{line}:{column} · subl {file}:{line} · open -a TextEdit {file}")
                    .font(.caption).foregroundStyle(.secondary)
            } header: {
                Text("Open results")
            }
            Section {
                Toggle("Open each search in a new tab", isOn: $openInNewTab)
                Toggle("Reopen tabs from the last session", isOn: $restoreSession)
                Stepper("Remember \(historyLimit) recent values per field", value: $historyLimit, in: 5...200, step: 5)
                    .onChange(of: historyLimit) { _, limit in app.setHistoryLimit(limit) }
            } header: {
                Text("Behavior")
            }
            Section {
                Stepper("Skip text files larger than \(maxFileSizeMB) MB", value: $maxFileSizeMB, in: 1...2048)
                Stepper("At most \(maxMatchesPerFile.formatted()) lines per file", value: $maxMatchesPerFile, in: 100...100_000, step: 100)
                Stepper("At most \(maxTotalMatches.formatted()) matches per search", value: $maxTotalMatches, in: 1_000...2_000_000, step: 10_000)
            } header: {
                Text("Limits")
            }
        }
        .formStyle(.grouped)
    }
}

private struct HistorySettings: View {
    @Bindable var app: AppState
    @State private var field: HistoryField = .directories

    var body: some View {
        VStack(alignment: .leading) {
            Picker("Field", selection: $field) {
                Text("Folders").tag(HistoryField.directories)
                Text("Find").tag(HistoryField.patterns)
                Text("Replace").tag(HistoryField.replacements)
                Text("File masks").tag(HistoryField.includeMasks)
                Text("Exclude masks").tag(HistoryField.excludeMasks)
            }
            .pickerStyle(.segmented)
            .labelsHidden()

            List {
                ForEach(app.history.entries(field), id: \.value) { entry in
                    HStack {
                        Button {
                            app.history.setPinned(!entry.pinned, value: entry.value, in: field)
                            app.saveHistory()
                        } label: {
                            Image(systemName: entry.pinned ? "star.fill" : "star")
                                .foregroundStyle(entry.pinned ? .yellow : .secondary)
                        }
                        .buttonStyle(.borderless)
                        .help(entry.pinned ? "Unpin" : "Pin to the top; pinned values are never dropped")
                        Text(entry.value).lineLimit(1).truncationMode(.middle).textSelection(.enabled)
                        Spacer()
                        Text(entry.lastUsed.formatted(.relative(presentation: .named)))
                            .font(.caption).foregroundStyle(.secondary)
                        Button {
                            app.history.remove(entry.value, from: field)
                            app.saveHistory()
                        } label: {
                            Image(systemName: "minus.circle")
                        }
                        .buttonStyle(.borderless)
                        .help("Remove")
                    }
                }
            }
            .listStyle(.inset(alternatesRowBackgrounds: true))

            HStack {
                Text("Pinned values stay at the top and survive clearing.").font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button("Clear Unpinned") {
                    app.history.clear(field)
                    app.saveHistory()
                }
            }
        }
        .padding()
    }
}

private struct StorageSettings: View {
    @State private var cacheSize: Int64 = 0
    @State private var backupsSize: Int64 = 0
    @State private var batches: [ReplaceBatch] = []
    @State private var message = ""

    var body: some View {
        Form {
            Section {
                LabeledContent("Size", value: ByteCountFormatter.string(fromByteCount: cacheSize, countStyle: .file))
                HStack {
                    Button("Remove Stale Entries") {
                        let report = ExtractionCache.shared.removeStale()
                        message = "Removed \(report.removed) stale entries, kept \(report.kept)."
                        refresh()
                    }
                    Button("Clear Cache") {
                        ExtractionCache.shared.clear()
                        message = "Cache cleared."
                        refresh()
                    }
                }
            } header: {
                Text("Document text cache")
            } footer: {
                Text("Text extracted from PDF and office documents is cached so later searches are fast.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section {
                LabeledContent("Backups", value: "\(batches.count) replaces, \(ByteCountFormatter.string(fromByteCount: backupsSize, countStyle: .file))")
                if let last = batches.first {
                    LabeledContent("Last replace", value: "“\(last.pattern)” → “\(last.replacement)”, \(last.entries.count) files")
                }
                HStack {
                    Button("Show in Finder") {
                        AppPaths.ensure(AppPaths.backups)
                        NSWorkspace.shared.open(AppPaths.backups)
                    }
                    Button("Delete All Backups", role: .destructive) {
                        ReplaceExecutor.clearBackups()
                        message = "Backups deleted."
                        refresh()
                    }
                }
            } header: {
                Text("Replace backups")
            }
            if !message.isEmpty {
                Text(message).font(.caption).foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .onAppear(perform: refresh)
    }

    private func refresh() {
        cacheSize = ExtractionCache.shared.sizeOnDisk
        backupsSize = AppPaths.folderSize(AppPaths.backups)
        batches = ReplaceExecutor.batches()
    }
}
