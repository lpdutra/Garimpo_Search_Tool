import AppKit
import GarimpoCore
import SwiftUI

@main
struct GarimpoApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @State private var app = AppState()

    var body: some Scene {
        Window("Garimpo", id: "main") {
            ContentView(app: app)
                .onAppear { delegate.app = app }
        }
        .defaultSize(width: 1280, height: 820)
        .commands { GarimpoCommands(app: app) }

        Settings {
            SettingsView(app: app)
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var app: AppState? {
        didSet { flushPendingURLs() }
    }
    private var pendingURLs: [URL] = []

    func applicationWillFinishLaunching(_ notification: Notification) {
        // Needed when the binary runs outside an app bundle, e.g. `swift run`.
        NSApp.setActivationPolicy(.regular)
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.activate()
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        pendingURLs += urls
        flushPendingURLs()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationWillTerminate(_ notification: Notification) {
        MainActor.assumeIsolated { app?.applicationWillTerminate() }
    }

    private func flushPendingURLs() {
        guard let app, !pendingURLs.isEmpty else { return }
        let urls = pendingURLs
        pendingURLs = []
        MainActor.assumeIsolated { app.openDocuments(urls) }
    }
}

struct GarimpoCommands: Commands {
    let app: AppState

    var body: some Commands {
        CommandGroup(replacing: .newItem) {
            Button("New Search") { app.focusPatternToken += 1 }
                .keyboardShortcut("n")
            Button("Open Results…") { app.showOpenPanel() }
                .keyboardShortcut("o")
        }
        CommandGroup(replacing: .saveItem) {
            Button("Close Tab") { app.closeSelectedTabOrWindow() }
                .keyboardShortcut("w")
            Divider()
            Button("Save Results") { app.save() }
                .keyboardShortcut("s")
                .disabled(app.selectedTab == nil)
            Button("Save Results As…") { app.save(saveAs: true) }
                .keyboardShortcut("s", modifiers: [.command, .shift])
                .disabled(app.selectedTab == nil)
            Menu("Export") {
                ForEach(ExportFormat.allCases, id: \.self) { format in
                    Button("\(format.title)…") { app.export(format) }
                }
            }
            .disabled(app.selectedTab == nil)
        }
        CommandGroup(replacing: .printItem) {
            Button("Print Results…") { app.print() }
                .keyboardShortcut("p")
                .disabled(app.selectedTab == nil)
        }
        CommandMenu("Search") {
            Button("Search") { app.search() }
                .keyboardShortcut(.return, modifiers: .command)
            Button("Search Again") { app.searchAgain() }
                .keyboardShortcut("r")
                .disabled(app.selectedTab == nil)
            Button("Stop") { app.stop() }
                .keyboardShortcut(".")
                .disabled(app.selectedTab?.isRunning != true)
            Divider()
            Button("Replace…") { app.beginReplace() }
                .keyboardShortcut("f", modifiers: [.command, .option])
            Button("Undo Last Replace…") { app.undoLastReplace() }
                .keyboardShortcut("z", modifiers: [.command, .option])
            Divider()
            Button("Next Match") { app.selectedTab?.selectMatch(offset: 1) }
                .keyboardShortcut("g")
            Button("Previous Match") { app.selectedTab?.selectMatch(offset: -1) }
                .keyboardShortcut("g", modifiers: [.command, .shift])
            Button("Next File") { app.selectedTab?.selectFile(offset: 1) }
                .keyboardShortcut(.downArrow, modifiers: [.command, .option])
            Button("Previous File") { app.selectedTab?.selectFile(offset: -1) }
                .keyboardShortcut(.upArrow, modifiers: [.command, .option])
            Divider()
            Button("Open in Editor") { app.openSelection() }
                .keyboardShortcut("o", modifiers: [.command, .option])
            Button("Show in Finder") {
                if let file = app.selectedTab?.selectedFile { FileActions.revealInFinder(paths: [file.path]) }
            }
            .keyboardShortcut("r", modifiers: [.command, .shift])
        }
        CommandGroup(after: .windowArrangement) {
            Divider()
            Button("Next Tab") { app.selectAdjacentTab(1) }
                .keyboardShortcut("]", modifiers: [.command, .shift])
            Button("Previous Tab") { app.selectAdjacentTab(-1) }
                .keyboardShortcut("[", modifiers: [.command, .shift])
            Button("Move Tab Left") { if let tab = app.selectedTab { app.move(tab, by: -1) } }
                .keyboardShortcut(.leftArrow, modifiers: [.command, .control])
            Button("Move Tab Right") { if let tab = app.selectedTab { app.move(tab, by: 1) } }
                .keyboardShortcut(.rightArrow, modifiers: [.command, .control])
        }
    }
}
