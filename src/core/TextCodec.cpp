#include "TextCodec.h"

#include <QFile>
#include <QStringDecoder>
#include <QStringEncoder>

#include <cstring>

namespace garimpo {

namespace {

// Windows-1252 code points for bytes 0x80-0x9F. Unassigned bytes map to the same C1 control,
// so every byte sequence decodes and encodes back unchanged.
constexpr char16_t kWindows1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

const QByteArray kUtf8Bom("\xEF\xBB\xBF", 3);
const QByteArray kUtf16LEBom("\xFF\xFE", 2);
const QByteArray kUtf16BEBom("\xFE\xFF", 2);

std::optional<QString> decodeStrict(QByteArrayView data, QStringConverter::Encoding encoding)
{
    QStringDecoder decoder(encoding, QStringConverter::Flag::Stateless);
    QString text = decoder.decode(data);
    if (decoder.hasError()) return std::nullopt;
    return text;
}

} // namespace

QString encodingName(TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8: return QStringLiteral("UTF-8");
    case TextEncoding::Utf8Bom: return QStringLiteral("UTF-8 BOM");
    case TextEncoding::Utf16LE: return QStringLiteral("UTF-16 LE");
    case TextEncoding::Utf16BE: return QStringLiteral("UTF-16 BE");
    case TextEncoding::Windows1252: return QStringLiteral("Windows-1252");
    }
    return {};
}

bool TextCodec::looksBinary(const QByteArray &data)
{
    const qsizetype length = qMin(data.size(), sniffLength);
    return std::memchr(data.constData(), 0, size_t(length)) != nullptr;
}

QString TextCodec::decodeWindows1252(QByteArrayView data)
{
    QString text(data.size(), Qt::Uninitialized);
    char16_t *out = reinterpret_cast<char16_t *>(text.data());
    for (qsizetype i = 0; i < data.size(); ++i) {
        const auto byte = static_cast<unsigned char>(data[i]);
        out[i] = (byte >= 0x80 && byte <= 0x9F) ? kWindows1252High[byte - 0x80] : char16_t(byte);
    }
    return text;
}

std::optional<DecodedText> TextCodec::decode(const QByteArray &data, DecodeError *error)
{
    const QByteArrayView view(data);
    if (data.startsWith(kUtf8Bom)) {
        if (auto text = decodeStrict(view.sliced(3), QStringConverter::Utf8)) return DecodedText{*text, TextEncoding::Utf8Bom};
    } else if (data.startsWith(kUtf16LEBom)) {
        if (auto text = decodeStrict(view.sliced(2), QStringConverter::Utf16LE)) return DecodedText{*text, TextEncoding::Utf16LE};
    } else if (data.startsWith(kUtf16BEBom)) {
        if (auto text = decodeStrict(view.sliced(2), QStringConverter::Utf16BE)) return DecodedText{*text, TextEncoding::Utf16BE};
    } else if (looksBinary(data)) {
        if (error) *error = DecodeError::Binary;
        return std::nullopt;
    } else if (auto text = decodeStrict(view, QStringConverter::Utf8)) {
        return DecodedText{*text, TextEncoding::Utf8};
    } else {
        return DecodedText{decodeWindows1252(view), TextEncoding::Windows1252};
    }
    if (error) *error = DecodeError::Unreadable;
    return std::nullopt;
}

std::optional<DecodedText> TextCodec::decodeFile(const QString &path, qint64 maxBytes, DecodeError *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || (maxBytes > 0 && file.size() > maxBytes)) {
        if (error) *error = DecodeError::Unreadable;
        return std::nullopt;
    }
    return decode(file.readAll(), error);
}

std::optional<QByteArray> TextCodec::encode(const QString &text, TextEncoding encoding)
{
    auto encodeWith = [&](QStringConverter::Encoding target, const QByteArray &bom) -> std::optional<QByteArray> {
        QStringEncoder encoder(target, QStringConverter::Flag::Stateless);
        QByteArray body = encoder.encode(text);
        if (encoder.hasError()) return std::nullopt;
        return bom + body;
    };

    switch (encoding) {
    case TextEncoding::Utf8: return encodeWith(QStringConverter::Utf8, {});
    case TextEncoding::Utf8Bom: return encodeWith(QStringConverter::Utf8, kUtf8Bom);
    case TextEncoding::Utf16LE: return encodeWith(QStringConverter::Utf16LE, kUtf16LEBom);
    case TextEncoding::Utf16BE: return encodeWith(QStringConverter::Utf16BE, kUtf16BEBom);
    case TextEncoding::Windows1252: {
        QByteArray out(text.size(), Qt::Uninitialized);
        for (qsizetype i = 0; i < text.size(); ++i) {
            const char16_t ch = text.at(i).unicode();
            if (ch < 0x80 || (ch >= 0xA0 && ch <= 0xFF)) {
                out[i] = char(ch);
                continue;
            }
            const char16_t *found = std::find(std::begin(kWindows1252High), std::end(kWindows1252High), ch);
            if (found == std::end(kWindows1252High)) return std::nullopt;
            out[i] = char(0x80 + (found - std::begin(kWindows1252High)));
        }
        return out;
    }
    }
    return std::nullopt;
}

} // namespace garimpo
