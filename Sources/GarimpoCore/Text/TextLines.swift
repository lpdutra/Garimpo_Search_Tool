import Foundation

public struct TextLine: Sendable {
    public var content: String
    /// `"\n"`, `"\r\n"`, `"\r"` or empty for the last line.
    public var terminator: String
}

public enum TextLines {
    /// Splits text into lines, keeping each line's own terminator so mixed line endings survive a rewrite.
    public static func split(_ text: String) -> [TextLine] {
        var lines: [TextLine] = []
        let utf8 = text.utf8
        var lineStart = utf8.startIndex
        var index = utf8.startIndex

        while index != utf8.endIndex {
            let byte = utf8[index]
            if byte == 0x0A || byte == 0x0D {
                let content = String(text[lineStart..<index])
                var next = utf8.index(after: index)
                if byte == 0x0D, next != utf8.endIndex, utf8[next] == 0x0A {
                    next = utf8.index(after: next)
                    lines.append(TextLine(content: content, terminator: "\r\n"))
                } else {
                    lines.append(TextLine(content: content, terminator: byte == 0x0A ? "\n" : "\r"))
                }
                lineStart = next
                index = next
            } else {
                index = utf8.index(after: index)
            }
        }
        if lineStart != utf8.endIndex || lines.isEmpty {
            lines.append(TextLine(content: String(text[lineStart...]), terminator: ""))
        }
        return lines
    }

    public static func join(_ lines: [TextLine]) -> String {
        lines.reduce(into: "") { $0 += $1.content + $1.terminator }
    }

    /// UTF-16 offset where each line starts, matching how `split` divides the text.
    public static func lineOffsets(_ lines: [TextLine]) -> [Int] {
        var offsets: [Int] = []
        offsets.reserveCapacity(lines.count)
        var offset = 0
        for line in lines {
            offsets.append(offset)
            offset += line.content.utf16.count + line.terminator.utf16.count
        }
        return offsets
    }
}
