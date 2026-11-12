import Foundation
import GarimpoCore

/// UserDefaults keys and defaults shared by `@AppStorage` and non-view code.
enum Preferences {
    static let editorCommand = "editorCommand"
    static let openInNewTab = "openInNewTab"
    static let restoreSession = "restoreSession"
    static let historyLimit = "historyLimit"
    static let maxFileSizeMB = "maxFileSizeMB"
    static let maxMatchesPerFile = "maxMatchesPerFile"
    static let maxTotalMatches = "maxTotalMatches"
    static let previewFontSize = "previewFontSize"

    static var defaultEditorCommand: String {
        let candidates = ["/opt/homebrew/bin/code", "/usr/local/bin/code"]
        return candidates.contains { FileManager.default.isExecutableFile(atPath: $0) }
            ? #"code -g {file}:{line}:{column}"#
            : ""
    }

    static func registerDefaults() {
        UserDefaults.standard.register(defaults: [
            editorCommand: defaultEditorCommand,
            openInNewTab: true,
            restoreSession: true,
            historyLimit: History.defaultLimit,
            maxFileSizeMB: 64,
            maxMatchesPerFile: 5_000,
            maxTotalMatches: 200_000,
            previewFontSize: 12.0,
        ])
    }

    static var limits: SearchLimits {
        let defaults = UserDefaults.standard
        return SearchLimits(
            maxMatchesPerFile: max(1, defaults.integer(forKey: maxMatchesPerFile)),
            maxTotalMatches: max(1, defaults.integer(forKey: maxTotalMatches)),
            maxFileSizeMB: max(1, defaults.integer(forKey: maxFileSizeMB))
        )
    }
}
