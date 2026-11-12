import AppKit
import GarimpoCore

@MainActor
enum ResultPrinter {
    static func print(_ document: ResultDocument) {
        let html = ResultExporter.html(document)
        guard let data = html.data(using: .utf8),
              let attributed = NSAttributedString(html: data, options: [.characterEncoding: String.Encoding.utf8.rawValue],
                                                  documentAttributes: nil)
        else { return }

        let info = NSPrintInfo.shared.copy() as! NSPrintInfo
        info.horizontalPagination = .fit
        info.verticalPagination = .automatic
        info.isHorizontallyCentered = false
        info.isVerticallyCentered = false

        let width = info.paperSize.width - info.leftMargin - info.rightMargin
        let textView = NSTextView(frame: NSRect(x: 0, y: 0, width: width, height: 1))
        textView.textStorage?.setAttributedString(attributed)
        textView.isVerticallyResizable = true
        textView.sizeToFit()

        let operation = NSPrintOperation(view: textView, printInfo: info)
        operation.jobTitle = document.title
        operation.showsPrintPanel = true
        operation.showsProgressPanel = true
        if let window = NSApp.keyWindow {
            operation.runModal(for: window, delegate: nil, didRun: nil, contextInfo: nil)
        } else {
            operation.run()
        }
    }
}
