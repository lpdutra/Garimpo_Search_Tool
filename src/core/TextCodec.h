#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace garimpo {

enum class TextEncoding : quint8 { Utf8, Utf8Bom, Utf16LE, Utf16BE, Windows1252 };

QString encodingName(TextEncoding encoding);

struct DecodedText {
    QString text;
    TextEncoding encoding = TextEncoding::Utf8;
};

enum class DecodeError { Binary, Unreadable };

/// Reads text in UTF-8, UTF-16 with BOM, or Windows-1252 as the fallback for legacy files.
class TextCodec {
public:
    static constexpr qsizetype sniffLength = 8192;

    static std::optional<DecodedText> decode(const QByteArray &data, DecodeError *error = nullptr);
    static std::optional<DecodedText> decodeFile(const QString &path, qint64 maxBytes, DecodeError *error = nullptr);

    /// Encodes text back with the encoding (and BOM) it was read with. Fails when a character does not fit.
    static std::optional<QByteArray> encode(const QString &text, TextEncoding encoding);

    static bool looksBinary(const QByteArray &data);
    static QString decodeWindows1252(QByteArrayView data);
};

} // namespace garimpo
