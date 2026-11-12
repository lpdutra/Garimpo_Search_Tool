import AppKit
import SwiftUI

/// Editable text field with a drop-down of previously used values.
struct HistoryComboBox: NSViewRepresentable {
    @Binding var text: String
    var items: [String]
    var placeholder: String = ""
    var monospaced = false
    var focusToken = 0
    var onSubmit: () -> Void = {}

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeNSView(context: Context) -> NSComboBox {
        let box = NSComboBox()
        box.usesDataSource = false
        box.completes = true
        box.numberOfVisibleItems = 15
        box.isButtonBordered = true
        box.placeholderString = placeholder
        box.delegate = context.coordinator
        box.lineBreakMode = .byTruncatingMiddle
        box.cell?.isScrollable = true
        box.cell?.wraps = false
        box.setContentHuggingPriority(.defaultLow, for: .horizontal)
        box.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        if monospaced {
            box.font = .monospacedSystemFont(ofSize: NSFont.systemFontSize, weight: .regular)
        }
        context.coordinator.focusToken = focusToken
        return box
    }

    func updateNSView(_ box: NSComboBox, context: Context) {
        context.coordinator.parent = self
        let current = box.objectValues.compactMap { $0 as? String }
        if current != items {
            box.removeAllItems()
            box.addItems(withObjectValues: items)
        }
        // Do not reset the field while the user is typing in it.
        if box.stringValue != text, box.currentEditor() == nil || context.coordinator.externalChange(text) {
            box.stringValue = text
        }
        if context.coordinator.focusToken != focusToken {
            context.coordinator.focusToken = focusToken
            DispatchQueue.main.async {
                box.window?.makeFirstResponder(box)
                box.currentEditor()?.selectAll(nil)
            }
        }
    }

    final class Coordinator: NSObject, NSComboBoxDelegate {
        var parent: HistoryComboBox
        var focusToken = 0
        private var lastTyped: String?

        init(_ parent: HistoryComboBox) {
            self.parent = parent
        }

        /// True when the binding changed from outside (menu, drag and drop) while the field is being edited.
        func externalChange(_ text: String) -> Bool {
            text != lastTyped
        }

        func controlTextDidChange(_ notification: Notification) {
            guard let box = notification.object as? NSComboBox else { return }
            lastTyped = box.stringValue
            parent.text = box.stringValue
        }

        func comboBoxSelectionDidChange(_ notification: Notification) {
            guard let box = notification.object as? NSComboBox,
                  let value = box.objectValueOfSelectedItem as? String else { return }
            lastTyped = value
            parent.text = value
        }

        func controlTextDidEndEditing(_ notification: Notification) {
            guard let box = notification.object as? NSComboBox else { return }
            lastTyped = box.stringValue
            if parent.text != box.stringValue { parent.text = box.stringValue }
        }

        func control(_ control: NSControl, textView: NSTextView, doCommandBy selector: Selector) -> Bool {
            guard selector == #selector(NSResponder.insertNewline(_:)) else { return false }
            if let box = control as? NSComboBox {
                lastTyped = box.stringValue
                parent.text = box.stringValue
            }
            parent.onSubmit()
            return true
        }
    }
}
