import AppKit
import GarimpoCore
import SwiftUI

struct PreviewContent: Equatable {
    var path: String
    var text: String
    var lineOffsets: [Int]
    var lineLengths: [Int]
    var encoding: String?
    var lineCount: Int { lineOffsets.count }

    /// Larger files are shown cut, to keep the text view responsive.
    static let maxCharacters = 4_000_000

    static func load(_ file: FileResult) -> Result<PreviewContent, Error> {
        do {
            var encoding: String?
            var text: String
            if file.kind == .document {
                let candidate = FileCandidate(url: file.url, size: file.size, modified: file.modified)
                guard let extracted = ExtractionCache.shared.text(for: candidate) else { throw TextDecoder.Failure.unreadable }
                text = extracted
            } else {
                let decoded = try TextDecoder.decode(contentsOf: file.url)
                text = decoded.text
                encoding = decoded.encodingName
            }
            if text.utf16.count > maxCharacters {
                text = String(text.prefix(maxCharacters))
            }
            let lines = TextLines.split(text)
            return .success(PreviewContent(path: file.path, text: text, lineOffsets: TextLines.lineOffsets(lines),
                                           lineLengths: lines.map { $0.content.utf16.count }, encoding: encoding))
        } catch {
            return .failure(error)
        }
    }
}

struct PreviewPane: View {
    let tab: SearchTab
    @State private var content: PreviewContent?
    @State private var failure: String?
    @AppStorage(Preferences.previewFontSize) private var fontSize = 12.0

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            if let file = tab.selectedFile, let content, content.path == file.path {
                PreviewTextView(content: content, file: file, selectedMatch: tab.selectedMatch, fontSize: fontSize)
            } else if let failure {
                placeholder(failure, systemImage: "doc.questionmark")
            } else if tab.selectedFile == nil {
                placeholder("Select a result to preview it", systemImage: "doc.text.magnifyingglass")
            } else {
                ProgressView().frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .task(id: tab.selectedFile?.path) {
            guard let file = tab.selectedFile else { content = nil; return }
            failure = nil
            let result = await Task.detached(priority: .userInitiated) { PreviewContent.load(file) }.value
            guard !Task.isCancelled else { return }
            switch result {
            case .success(let loaded): content = loaded
            case .failure: content = nil; failure = "This file cannot be previewed"
            }
        }
    }

    private var header: some View {
        HStack(spacing: 8) {
            if let file = tab.selectedFile {
                Text((file.path as NSString).abbreviatingWithTildeInPath)
                    .font(.callout)
                    .lineLimit(1)
                    .truncationMode(.head)
                    .textSelection(.enabled)
                Spacer()
                if let match = tab.selectedMatch {
                    Text("Line \(match.lineNumber), column \(match.column)")
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
                if let encoding = content?.encoding, content?.path == file.path {
                    Text(encoding).font(.caption).foregroundStyle(.secondary)
                }
                Button {
                    fontSize = max(8, fontSize - 1)
                } label: { Image(systemName: "textformat.size.smaller") }
                .buttonStyle(.borderless)
                .help("Smaller text")
                Button {
                    fontSize = min(28, fontSize + 1)
                } label: { Image(systemName: "textformat.size.larger") }
                .buttonStyle(.borderless)
                .help("Larger text")
            } else {
                Text("Preview").foregroundStyle(.secondary)
                Spacer()
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
    }

    private func placeholder(_ text: String, systemImage: String) -> some View {
        VStack(spacing: 8) {
            Image(systemName: systemImage).font(.largeTitle).foregroundStyle(.tertiary)
            Text(text).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

/// Read-only text view with line numbers, all matches highlighted and the selected line scrolled into view.
struct PreviewTextView: NSViewRepresentable {
    let content: PreviewContent
    let file: FileResult
    let selectedMatch: LineMatch?
    let fontSize: Double

    final class Coordinator {
        var shownPath: String?
        var shownFontSize: Double?
        var shownMatches: [LineMatch] = []
        var currentHighlight: [NSRange] = []
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> NSScrollView {
        let scrollView = NSTextView.scrollableTextView()
        let textView = scrollView.documentView as! NSTextView
        textView.isEditable = false
        textView.isSelectable = true
        textView.isRichText = false
        textView.usesFindBar = true
        textView.isIncrementalSearchingEnabled = true
        textView.textContainerInset = NSSize(width: 6, height: 6)
        textView.isHorizontallyResizable = true
        textView.textContainer?.widthTracksTextView = false
        textView.textContainer?.containerSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
        scrollView.hasHorizontalScroller = true
        scrollView.verticalRulerView = LineNumberRuler(textView: textView)
        scrollView.hasVerticalRuler = true
        scrollView.rulersVisible = true
        return scrollView
    }

    func updateNSView(_ scrollView: NSScrollView, context: Context) {
        guard let textView = scrollView.documentView as? NSTextView, let storage = textView.textStorage else { return }
        let coordinator = context.coordinator
        let font = NSFont.monospacedSystemFont(ofSize: fontSize, weight: .regular)

        if coordinator.shownPath != content.path || coordinator.shownFontSize != fontSize || coordinator.shownMatches != file.matches {
            let attributed = NSMutableAttributedString(string: content.text, attributes: [
                .font: font,
                .foregroundColor: NSColor.textColor,
            ])
            for match in file.matches {
                for range in ranges(of: match) {
                    attributed.addAttribute(.backgroundColor, value: NSColor.systemYellow.withAlphaComponent(0.35), range: range)
                }
            }
            storage.setAttributedString(attributed)
            coordinator.shownPath = content.path
            coordinator.shownFontSize = fontSize
            coordinator.shownMatches = file.matches
            coordinator.currentHighlight = []
            (scrollView.verticalRulerView as? LineNumberRuler)?.lineOffsets = content.lineOffsets
        }

        guard let layoutManager = textView.layoutManager else { return }
        for range in coordinator.currentHighlight where NSMaxRange(range) <= storage.length {
            layoutManager.removeTemporaryAttribute(.backgroundColor, forCharacterRange: range)
        }
        coordinator.currentHighlight = []

        guard let match = selectedMatch, match.lineNumber - 1 < content.lineCount else {
            scrollView.verticalRulerView?.needsDisplay = true
            return
        }
        let lineIndex = match.lineNumber - 1
        let lineRange = NSRange(location: content.lineOffsets[lineIndex], length: content.lineLengths[lineIndex])
        guard NSMaxRange(lineRange) <= storage.length else { return }
        layoutManager.addTemporaryAttribute(.backgroundColor, value: NSColor.selectedTextBackgroundColor.withAlphaComponent(0.45),
                                            forCharacterRange: lineRange)
        coordinator.currentHighlight.append(lineRange)
        for range in ranges(of: match) {
            layoutManager.addTemporaryAttribute(.backgroundColor, value: NSColor.systemOrange.withAlphaComponent(0.8), forCharacterRange: range)
            coordinator.currentHighlight.append(range)
        }
        let target = ranges(of: match).first ?? lineRange
        DispatchQueue.main.async {
            textView.scrollRangeToVisible(target)
            centerVertically(textView, on: target)
            scrollView.verticalRulerView?.needsDisplay = true
        }
    }

    /// Absolute ranges of a match inside the preview text.
    private func ranges(of match: LineMatch) -> [NSRange] {
        let lineIndex = match.lineNumber - 1
        guard content.lineOffsets.indices.contains(lineIndex) else { return [] }
        let start = content.lineOffsets[lineIndex]
        let length = content.lineLengths[lineIndex]
        // Stored text may be a clipped window of a very long line; find where it starts.
        var shift = 0
        if match.text.utf16.count < length {
            let line = (content.text as NSString).substring(with: NSRange(location: start, length: length)) as NSString
            let found = line.range(of: match.text)
            if found.location != NSNotFound { shift = found.location }
        }
        return match.spans.compactMap { span in
            guard span.location + shift + span.length <= length else { return nil }
            return NSRange(location: start + shift + span.location, length: span.length)
        }
    }

    private func centerVertically(_ textView: NSTextView, on range: NSRange) {
        guard let layoutManager = textView.layoutManager, let container = textView.textContainer,
              let clipView = textView.enclosingScrollView?.contentView else { return }
        let glyphs = layoutManager.glyphRange(forCharacterRange: range, actualCharacterRange: nil)
        let rect = layoutManager.boundingRect(forGlyphRange: glyphs, in: container)
        let visible = clipView.bounds
        let y = max(0, rect.midY - visible.height / 2)
        // Scroll sideways only as much as needed to reveal the match.
        // The clip view extends under the line-number ruler, so the left edge sits at -inset.
        let inset = clipView.contentInsets.left
        let usable = visible.width - inset
        let x = rect.maxX + 40 > usable ? rect.maxX + 40 - usable : 0
        clipView.scroll(to: NSPoint(x: x - inset, y: y - clipView.contentInsets.top))
        textView.enclosingScrollView?.reflectScrolledClipView(clipView)
    }
}

/// Line numbers drawn in the vertical ruler of the preview.
final class LineNumberRuler: NSRulerView {
    var lineOffsets: [Int] = [] {
        didSet {
            let digits = max(3, String(lineOffsets.count).count)
            ruleThickness = CGFloat(digits) * 8 + 14
            needsDisplay = true
        }
    }

    // Without this the ruler reserves marker space and the text view slides under it.
    override var requiredThickness: CGFloat { ruleThickness }

    init(textView: NSTextView) {
        super.init(scrollView: textView.enclosingScrollView, orientation: .verticalRuler)
        clientView = textView
        ruleThickness = 38
        NotificationCenter.default.addObserver(self, selector: #selector(redraw), name: NSView.boundsDidChangeNotification,
                                               object: textView.enclosingScrollView?.contentView)
    }

    required init(coder: NSCoder) { fatalError("not used") }

    @objc private func redraw() { needsDisplay = true }

    override func drawHashMarksAndLabels(in rect: NSRect) {
        guard let textView = clientView as? NSTextView, let layoutManager = textView.layoutManager,
              let container = textView.textContainer, !lineOffsets.isEmpty else { return }

        NSColor.windowBackgroundColor.withAlphaComponent(0.6).setFill()
        bounds.fill()

        let font = NSFont.monospacedDigitSystemFont(ofSize: max(9, (textView.font?.pointSize ?? 12) - 2), weight: .regular)
        let attributes: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: NSColor.secondaryLabelColor]
        let visible = textView.visibleRect
        let glyphRange = layoutManager.glyphRange(forBoundingRect: visible, in: container)
        let characterRange = layoutManager.characterRange(forGlyphRange: glyphRange, actualGlyphRange: nil)
        let inset = textView.textContainerInset.height
        let relative = convert(NSPoint.zero, from: textView)

        var line = lineIndex(containing: characterRange.location)
        while line < lineOffsets.count, lineOffsets[line] <= NSMaxRange(characterRange) {
            let offset = lineOffsets[line]
            guard offset <= (textView.string as NSString).length else { break }
            let glyph = layoutManager.glyphIndexForCharacter(at: offset)
            var lineRect = layoutManager.lineFragmentRect(forGlyphAt: glyph, effectiveRange: nil)
            lineRect.origin.y += inset + relative.y
            let label = String(line + 1) as NSString
            let size = label.size(withAttributes: attributes)
            label.draw(at: NSPoint(x: bounds.width - size.width - 6, y: lineRect.minY + (lineRect.height - size.height) / 2),
                       withAttributes: attributes)
            line += 1
        }
    }

    private func lineIndex(containing offset: Int) -> Int {
        var low = 0
        var high = lineOffsets.count - 1
        while low < high {
            let mid = (low + high + 1) / 2
            if lineOffsets[mid] <= offset { low = mid } else { high = mid - 1 }
        }
        return low
    }
}
