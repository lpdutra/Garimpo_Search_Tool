import SwiftUI

struct TabStrip: View {
    @Bindable var app: AppState
    @State private var renaming: SearchTab?
    @State private var newTitle = ""

    var body: some View {
        ScrollViewReader { proxy in
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 2) {
                    ForEach(app.tabs) { tab in
                        TabButton(tab: tab, isSelected: tab.id == app.selectedTabID,
                                  select: { app.select(tab) }, close: { app.close(tab) })
                            .id(tab.id)
                            .onTapGesture(count: 2) { startRename(tab) }
                            .contextMenu { menu(for: tab) }
                    }
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
            }
            .onChange(of: app.selectedTabID) { _, id in
                guard let id else { return }
                withAnimation { proxy.scrollTo(id) }
            }
        }
        .background(.bar)
        .alert("Rename Tab", isPresented: Binding(get: { renaming != nil }, set: { if !$0 { renaming = nil } })) {
            TextField("Title", text: $newTitle)
            Button("Rename") {
                if let tab = renaming { app.rename(tab, to: newTitle) }
                renaming = nil
            }
            Button("Cancel", role: .cancel) { renaming = nil }
        } message: {
            Text("Leave empty to use the search pattern.")
        }
    }

    private func startRename(_ tab: SearchTab) {
        newTitle = tab.title
        renaming = tab
    }

    @ViewBuilder
    private func menu(for tab: SearchTab) -> some View {
        Button("Rename…") { startRename(tab) }
        Button("Search Again") { app.searchAgain(tab) }
        Button("Load Query into Form") { app.loadQueryIntoForm(tab) }
        Divider()
        Button("Save…") { app.save(tab) }
        if let url = tab.fileURL {
            Button("Show Saved File in Finder") { FileActions.revealInFinder(paths: [url.path]) }
        }
        Divider()
        Button("Move Left") { app.move(tab, by: -1) }
            .disabled(app.tabs.first?.id == tab.id)
        Button("Move Right") { app.move(tab, by: 1) }
            .disabled(app.tabs.last?.id == tab.id)
        Divider()
        Button("Close Tab") { app.close(tab) }
        Button("Close Other Tabs") { app.closeOthers(than: tab) }
            .disabled(app.tabs.count < 2)
    }
}

private struct TabButton: View {
    let tab: SearchTab
    let isSelected: Bool
    let select: () -> Void
    let close: () -> Void
    @State private var hovering = false

    var body: some View {
        HStack(spacing: 6) {
            if tab.isRunning {
                ProgressView().controlSize(.mini)
            } else if case .failed = tab.state {
                Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange).imageScale(.small)
            }
            Text(tab.title)
                .lineLimit(1)
                .font(.callout.weight(isSelected ? .semibold : .regular))
            Text(tab.files.count.formatted())
                .font(.caption2.monospacedDigit())
                .padding(.horizontal, 5)
                .padding(.vertical, 1)
                .background(Capsule().fill(Color.secondary.opacity(0.18)))
            Button(action: close) {
                Image(systemName: "xmark").font(.system(size: 9, weight: .bold))
            }
            .buttonStyle(.plain)
            .foregroundStyle(.secondary)
            .opacity(hovering || isSelected ? 1 : 0)
            .help("Close Tab")
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .frame(maxWidth: 260)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(isSelected ? Color.accentColor.opacity(0.18) : (hovering ? Color.secondary.opacity(0.1) : .clear))
        )
        .contentShape(Rectangle())
        .onTapGesture(perform: select)
        .onHover { hovering = $0 }
        .help(tab.query.directories)
    }
}
