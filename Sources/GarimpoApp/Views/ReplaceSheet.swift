import GarimpoCore
import SwiftUI

struct ReplaceSheet: View {
    @Bindable var model: ReplaceModel
    let onCancel: () -> Void
    let onReplace: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            VStack(alignment: .leading, spacing: 4) {
                Text("Replace").font(.title2.bold())
                HStack(spacing: 6) {
                    Text(model.query.pattern).font(.system(.body, design: .monospaced)).foregroundStyle(.red)
                    Image(systemName: "arrow.right").foregroundStyle(.secondary)
                    Text(model.replacement.isEmpty ? "(empty)" : model.replacement)
                        .font(.system(.body, design: .monospaced)).foregroundStyle(.green)
                }
                Text(model.query.directories).font(.caption).foregroundStyle(.secondary).lineLimit(1).truncationMode(.middle)
            }

            switch model.state {
            case .preparing:
                VStack { ProgressView("Finding lines to change…") }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            case .failed(let message):
                Text(message).foregroundStyle(.red).frame(maxWidth: .infinity, maxHeight: .infinity)
            case .ready:
                if let plan = model.plan, !plan.files.isEmpty {
                    preview
                } else {
                    Text("No line would change.").foregroundStyle(.secondary).frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }

            if let skipped = model.plan?.skipped, !skipped.isEmpty {
                Text("\(skipped.count) files skipped (binary, unreadable or documents).")
                    .font(.caption).foregroundStyle(.secondary)
            }

            HStack {
                if model.state == .ready, model.plan?.files.isEmpty == false {
                    Button("Select All") { model.setAll(true) }
                    Button("Select None") { model.setAll(false) }
                }
                Spacer()
                Text("A backup of every changed file is kept; use Search ▸ Undo Last Replace to revert.")
                    .font(.caption).foregroundStyle(.secondary)
                Button("Cancel", role: .cancel, action: onCancel)
                    .keyboardShortcut(.cancelAction)
                Button(replaceTitle, action: onReplace)
                    .keyboardShortcut(.defaultAction)
                    .disabled(model.state != .ready || (model.plan?.selectedLineCount ?? 0) == 0)
            }
        }
        .padding(16)
        .frame(minWidth: 760, idealWidth: 900, minHeight: 480, idealHeight: 620)
    }

    private var replaceTitle: String {
        guard let plan = model.plan else { return "Replace" }
        return "Replace \(plan.selectedLineCount) lines in \(plan.selectedFileCount) files"
    }

    private var preview: some View {
        List {
            ForEach(Binding(get: { model.plan?.files ?? [] }, set: { model.plan?.files = $0 })) { $file in
                DisclosureGroup(isExpanded: .constant(true)) {
                    ForEach($file.lines) { $line in
                        HStack(alignment: .top, spacing: 8) {
                            Toggle("", isOn: $line.included).labelsHidden().disabled(!file.included)
                            Text(String(line.lineNumber))
                                .font(.system(.caption, design: .monospaced))
                                .foregroundStyle(.secondary)
                                .frame(minWidth: 40, alignment: .trailing)
                            VStack(alignment: .leading, spacing: 2) {
                                Text("− " + line.original.trimmingCharacters(in: .whitespaces))
                                    .foregroundStyle(.red)
                                Text("+ " + line.replaced.trimmingCharacters(in: .whitespaces))
                                    .foregroundStyle(.green)
                            }
                            .font(.system(.callout, design: .monospaced))
                            .lineLimit(2)
                            .textSelection(.enabled)
                        }
                        .opacity(file.included && line.included ? 1 : 0.45)
                    }
                } label: {
                    HStack {
                        Toggle("", isOn: $file.included).labelsHidden()
                        Text(file.name).bold()
                        Text((file.path as NSString).deletingLastPathComponent)
                            .font(.caption).foregroundStyle(.secondary).lineLimit(1).truncationMode(.middle)
                        Spacer()
                        Text(file.encoding).font(.caption).foregroundStyle(.secondary)
                        Text("\(file.selectedLineCount)/\(file.lines.count)").font(.caption.monospacedDigit())
                    }
                }
            }
        }
        .listStyle(.inset(alternatesRowBackgrounds: true))
    }
}
