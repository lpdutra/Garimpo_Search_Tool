import AppKit
import GarimpoCore

/// Opening files outside the app: external editor, default app, Finder, clipboard.
@MainActor
enum FileActions {
    /// Runs the editor command from Settings. `{file}`, `{line}` and `{column}` are substituted;
    /// the file path is shell-quoted. Falls back to the default app when no command is set.
    static func openInEditor(path: String, line: Int? = nil, column: Int? = nil) {
        let template = (UserDefaults.standard.string(forKey: Preferences.editorCommand) ?? "")
            .trimmingCharacters(in: .whitespaces)
        guard !template.isEmpty else {
            openWithDefaultApp(path: path)
            return
        }
        let command = template
            .replacingOccurrences(of: "{file}", with: shellQuote(path))
            .replacingOccurrences(of: "{line}", with: String(line ?? 1))
            .replacingOccurrences(of: "{column}", with: String(column ?? 1))

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/zsh")
        // A login shell picks up the user's PATH, which apps launched from Finder do not get.
        process.arguments = ["-lc", command]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        process.terminationHandler = { finished in
            guard finished.terminationStatus != 0 else { return }
            Task { @MainActor in openWithDefaultApp(path: path) }
        }
        do {
            try process.run()
        } catch {
            openWithDefaultApp(path: path)
        }
    }

    static func openWithDefaultApp(path: String) {
        NSWorkspace.shared.open(URL(fileURLWithPath: path))
    }

    static func revealInFinder(paths: [String]) {
        NSWorkspace.shared.activateFileViewerSelecting(paths.map { URL(fileURLWithPath: $0) })
    }

    static func copy(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    static func shellQuote(_ value: String) -> String {
        "'" + value.replacingOccurrences(of: "'", with: #"'\''"#) + "'"
    }
}
