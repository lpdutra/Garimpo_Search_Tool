import Foundation

public struct DecodedText: Sendable {
    public var text: String
    public var encoding: String.Encoding
    public var byteOrderMark: Data

    public var encodingName: String {
        switch encoding {
        case .utf8: byteOrderMark.isEmpty ? "UTF-8" : "UTF-8 BOM"
        case .utf16LittleEndian: "UTF-16 LE"
        case .utf16BigEndian: "UTF-16 BE"
        case .windowsCP1252: "Windows-1252"
        case .isoLatin1: "ISO-8859-1"
        default: String.localizedName(of: encoding)
        }
    }

    /// Encodes `newText` back with the same encoding and BOM it was read with.
    public func encode(_ newText: String) -> Data? {
        guard let body = newText.data(using: encoding, allowLossyConversion: false) else { return nil }
        return byteOrderMark + body
    }
}

public enum TextDecoder {
    public enum Failure: Error {
        case binary
        case unreadable
    }

    private static let sniffLength = 8192

    public static func decode(contentsOf url: URL) throws -> DecodedText {
        guard let data = try? Data(contentsOf: url, options: .mappedIfSafe) else { throw Failure.unreadable }
        return try decode(data)
    }

    public static func decode(_ data: Data) throws -> DecodedText {
        let utf8BOM = Data([0xEF, 0xBB, 0xBF])
        let utf16LE = Data([0xFF, 0xFE])
        let utf16BE = Data([0xFE, 0xFF])

        if data.starts(with: utf8BOM) {
            return try make(data, bom: utf8BOM, encoding: .utf8)
        }
        if data.starts(with: utf16LE) {
            return try make(data, bom: utf16LE, encoding: .utf16LittleEndian)
        }
        if data.starts(with: utf16BE) {
            return try make(data, bom: utf16BE, encoding: .utf16BigEndian)
        }
        if looksBinary(data) { throw Failure.binary }

        // Prefer strict UTF-8; legacy files are usually Windows-1252, and Latin-1 accepts any byte.
        for encoding in [String.Encoding.utf8, .windowsCP1252, .isoLatin1] {
            if let text = String(data: data, encoding: encoding) {
                return DecodedText(text: text, encoding: encoding, byteOrderMark: Data())
            }
        }
        throw Failure.unreadable
    }

    public static func looksBinary(_ data: Data) -> Bool {
        data.prefix(sniffLength).contains(0)
    }

    private static func make(_ data: Data, bom: Data, encoding: String.Encoding) throws -> DecodedText {
        guard let text = String(data: data.dropFirst(bom.count), encoding: encoding) else { throw Failure.unreadable }
        return DecodedText(text: text, encoding: encoding, byteOrderMark: bom)
    }
}
