import AppKit
import PDFKit

/// Pulls plain text out of formats that are not plain text files.
public enum DocumentExtractor {
    public static let supportedExtensions: Set<String> = ["pdf", "rtf", "rtfd", "doc", "docx", "odt", "webarchive"]

    public static func canExtract(_ url: URL) -> Bool {
        supportedExtensions.contains(url.pathExtension.lowercased())
    }

    public static func extractText(from url: URL) -> String? {
        switch url.pathExtension.lowercased() {
        case "pdf":
            return PDFDocument(url: url)?.string
        case "rtf":
            return attributedText(url, type: .rtf)
        case "rtfd":
            return attributedText(url, type: .rtfd)
        case "doc":
            return attributedText(url, type: .docFormat)
        case "docx":
            return attributedText(url, type: .officeOpenXML)
        case "odt":
            return attributedText(url, type: .openDocument)
        case "webarchive":
            return attributedText(url, type: .webArchive)
        default:
            return nil
        }
    }

    private static func attributedText(_ url: URL, type: NSAttributedString.DocumentType) -> String? {
        try? NSAttributedString(url: url, options: [.documentType: type], documentAttributes: nil).string
    }
}
