import GarimpoCore
import SwiftUI

struct ContentView: View {
    @Bindable var app: AppState

    var body: some View {
        VStack(spacing: 0) {
            SearchForm(app: app)
            Divider()
            if app.tabs.isEmpty {
                WelcomeView()
            } else {
                TabStrip(app: app)
                Divider()
                if let tab = app.selectedTab {
                    HSplitView {
                        ResultsView(app: app, tab: tab)
                            .frame(minWidth: 320, idealWidth: 480)
                        PreviewPane(tab: tab)
                            .frame(minWidth: 320)
                    }
                    Divider()
                    StatusBar(tab: tab)
                }
            }
        }
        .frame(minWidth: 900, minHeight: 560)
        .alert(item: $app.alert) { alert in
            Alert(title: Text(alert.title), message: Text(alert.message))
        }
        .sheet(item: $app.replaceModel) { model in
            ReplaceSheet(model: model, onCancel: {
                model.cancel()
                app.replaceModel = nil
            }, onReplace: {
                app.finishReplace(model)
            })
        }
    }
}

private struct WelcomeView: View {
    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "sparkle.magnifyingglass")
                .font(.system(size: 56))
                .foregroundStyle(.tint)
            Text("Garimpo").font(.largeTitle.bold())
            VStack(alignment: .leading, spacing: 6) {
                tip("Pick or drop a folder, type what to find and press Return.")
                tip("Leave Find empty to list every file matching the masks.")
                tip("Expression mode: invoice AND (paid OR \"due date\") NOT draft, /regex/ terms allowed.")
                tip("⌘G / ⇧⌘G move between matches, ⌥⌘↓ / ⌥⌘↑ between files, Return opens the editor.")
                tip("Folders, patterns and masks you use are remembered in each field’s list.")
            }
            .font(.callout)
            .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding()
    }

    private func tip(_ text: String) -> some View {
        Label(text, systemImage: "chevron.right").labelStyle(TipLabelStyle())
    }
}

private struct TipLabelStyle: LabelStyle {
    func makeBody(configuration: Configuration) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            configuration.icon.font(.caption2)
            configuration.title
        }
    }
}

private struct StatusBar: View {
    let tab: SearchTab

    var body: some View {
        HStack(spacing: 10) {
            if tab.isRunning { ProgressView().controlSize(.small) }
            Text(summary).font(.callout).foregroundStyle(.secondary)
            Spacer()
            if tab.stats.limitReached {
                Label("Result limit reached", systemImage: "exclamationmark.triangle")
                    .font(.callout).foregroundStyle(.orange)
            }
            if let url = tab.fileURL {
                Text(url.lastPathComponent + (tab.isDirty ? " (edited)" : ""))
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 5)
    }

    private var summary: String {
        let stats = tab.stats
        let found = "\(stats.matchCount.formatted()) matches in \(tab.files.count.formatted()) files"
        let scanned = "\(stats.filesScanned.formatted()) scanned"
        var skipped: [String] = []
        if stats.skippedBinary > 0 { skipped.append("\(stats.skippedBinary.formatted()) binary") }
        if stats.skippedUnreadable > 0 { skipped.append("\(stats.skippedUnreadable.formatted()) unreadable") }
        let skippedText = skipped.isEmpty ? "" : ", skipped " + skipped.joined(separator: ", ")
        let duration = stats.duration.formatted(.number.precision(.fractionLength(2))) + " s"

        switch tab.state {
        case .running: return "Searching… \(found) · \(scanned)"
        case .cancelled: return "Stopped · \(found) · \(scanned)\(skippedText)"
        case .failed(let message): return message
        case .idle: return ""
        case .finished:
            let when = tab.createdAt.formatted(date: .abbreviated, time: .shortened)
            return "\(found) · \(scanned)\(skippedText) · \(duration) · \(when)"
        }
    }
}
