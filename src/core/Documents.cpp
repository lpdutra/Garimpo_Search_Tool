#include "Documents.h"

#include "Storage.h"
#include "TextCodec.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QSet>
#include <QXmlStreamReader>
#include <QtEndian>

#include <cctype>
#include <zlib.h>

namespace garimpo {

namespace {

constexpr qint64 kMaxDocumentBytes = 200 * 1024 * 1024;

quint16 read16(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<quint16>(data.constData() + offset);
}

quint32 read32(const QByteArray &data, qsizetype offset)
{
    return qFromLittleEndian<quint32>(data.constData() + offset);
}

std::optional<QByteArray> inflateRaw(QByteArrayView compressed, qint64 expectedSize)
{
    if (expectedSize < 0 || expectedSize > kMaxDocumentBytes) return std::nullopt;
    QByteArray output(qsizetype(expectedSize), Qt::Uninitialized);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return std::nullopt;
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.data()));
    stream.avail_in = uInt(compressed.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = uInt(output.size());
    const int status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (status != Z_STREAM_END) return std::nullopt;
    output.resize(qsizetype(stream.total_out));
    return output;
}

QString extension(const QString &path)
{
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return dot > slash ? path.mid(dot + 1).toLower() : QString();
}

std::optional<QString> extractPdf(const QString &path)
{
    const QString tool = DocumentExtractor::pdfToTextPath();
    if (tool.isEmpty()) return std::nullopt;
    QProcess process;
    process.start(tool, {QStringLiteral("-q"), QStringLiteral("-enc"), QStringLiteral("UTF-8"), path, QStringLiteral("-")});
    if (!process.waitForFinished(120000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        process.kill();
        return std::nullopt;
    }
    QString text = QString::fromUtf8(process.readAllStandardOutput());
    // pdftotext separates pages with form feeds.
    text.replace(QChar(0x0C), QLatin1Char('\n'));
    return text;
}

} // namespace

// MARK: DocumentExtractor

bool DocumentExtractor::canExtract(const QString &path)
{
    static const QStringList supported{QStringLiteral("pdf"), QStringLiteral("docx"), QStringLiteral("odt"), QStringLiteral("rtf")};
    return supported.contains(extension(path));
}

QString DocumentExtractor::pdfToTextPath()
{
    static const QString path = [] {
        QString found = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
        if (found.isEmpty()) {
            found = QStandardPaths::findExecutable(QStringLiteral("pdftotext"),
                                                   {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
        }
        return found;
    }();
    return path;
}

std::optional<QString> DocumentExtractor::extractText(const QString &path)
{
    const QString kind = extension(path);
    if (kind == QLatin1String("pdf")) return extractPdf(path);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxDocumentBytes) return std::nullopt;
    const QByteArray data = file.readAll();

    if (kind == QLatin1String("rtf")) return textFromRtf(data);
    if (kind == QLatin1String("docx")) {
        const auto xml = readZipEntry(data, QStringLiteral("word/document.xml"));
        return xml ? std::optional<QString>(textFromWordXml(*xml)) : std::nullopt;
    }
    if (kind == QLatin1String("odt")) {
        const auto xml = readZipEntry(data, QStringLiteral("content.xml"));
        return xml ? std::optional<QString>(textFromOpenDocumentXml(*xml)) : std::nullopt;
    }
    return std::nullopt;
}

std::optional<QByteArray> DocumentExtractor::readZipEntry(const QByteArray &zip, const QString &entryName)
{
    // Find the end-of-central-directory record, which sits in the last 64 KB.
    const qsizetype minimum = 22;
    if (zip.size() < minimum) return std::nullopt;
    qsizetype eocd = -1;
    for (qsizetype i = zip.size() - minimum; i >= qMax<qsizetype>(0, zip.size() - minimum - 65535); --i) {
        if (read32(zip, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) return std::nullopt;

    const quint16 entries = read16(zip, eocd + 10);
    qsizetype offset = read32(zip, eocd + 16);
    const QByteArray wanted = entryName.toUtf8();

    for (int i = 0; i < entries; ++i) {
        if (offset + 46 > zip.size() || read32(zip, offset) != 0x02014b50) return std::nullopt;
        const quint16 method = read16(zip, offset + 10);
        const quint32 compressedSize = read32(zip, offset + 20);
        const quint32 uncompressedSize = read32(zip, offset + 24);
        const quint16 nameLength = read16(zip, offset + 28);
        const quint16 extraLength = read16(zip, offset + 30);
        const quint16 commentLength = read16(zip, offset + 32);
        const quint32 localOffset = read32(zip, offset + 42);
        if (offset + 46 + nameLength > zip.size()) return std::nullopt;
        const QByteArrayView name(zip.constData() + offset + 46, nameLength);

        if (name == wanted) {
            if (qsizetype(localOffset) + 30 > zip.size() || read32(zip, localOffset) != 0x04034b50) return std::nullopt;
            const qsizetype dataStart = localOffset + 30 + read16(zip, localOffset + 26) + read16(zip, localOffset + 28);
            if (dataStart + qsizetype(compressedSize) > zip.size()) return std::nullopt;
            const QByteArrayView payload(zip.constData() + dataStart, compressedSize);
            if (method == 0) return payload.toByteArray();
            if (method == 8) return inflateRaw(payload, uncompressedSize);
            return std::nullopt;
        }
        offset += 46 + nameLength + extraLength + commentLength;
    }
    return std::nullopt;
}

QString DocumentExtractor::textFromWordXml(const QByteArray &xml)
{
    QString text;
    QXmlStreamReader reader(xml);
    bool inText = false;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement: {
            const auto name = reader.name();
            if (name == QLatin1String("t")) inText = true;
            else if (name == QLatin1String("tab")) text.append(QLatin1Char('\t'));
            else if (name == QLatin1String("br") || name == QLatin1String("cr")) text.append(QLatin1Char('\n'));
            break;
        }
        case QXmlStreamReader::EndElement: {
            const auto name = reader.name();
            if (name == QLatin1String("t")) inText = false;
            else if (name == QLatin1String("p")) text.append(QLatin1Char('\n'));
            break;
        }
        case QXmlStreamReader::Characters:
            if (inText) text.append(reader.text());
            break;
        default:
            break;
        }
    }
    return text;
}

QString DocumentExtractor::textFromOpenDocumentXml(const QByteArray &xml)
{
    QString text;
    QXmlStreamReader reader(xml);
    int paragraphDepth = 0;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement: {
            const auto name = reader.name();
            if (name == QLatin1String("p") || name == QLatin1String("h")) ++paragraphDepth;
            else if (name == QLatin1String("tab")) text.append(QLatin1Char('\t'));
            else if (name == QLatin1String("line-break")) text.append(QLatin1Char('\n'));
            else if (name == QLatin1String("s")) {
                const int count = qMax(1, reader.attributes().value(QLatin1String("text:c")).toInt());
                text.append(QString(count, QLatin1Char(' ')));
            }
            break;
        }
        case QXmlStreamReader::EndElement: {
            const auto name = reader.name();
            if (name == QLatin1String("p") || name == QLatin1String("h")) {
                --paragraphDepth;
                text.append(QLatin1Char('\n'));
            }
            break;
        }
        case QXmlStreamReader::Characters:
            if (paragraphDepth > 0) text.append(reader.text());
            break;
        default:
            break;
        }
    }
    return text;
}

QString DocumentExtractor::textFromRtf(const QByteArray &rtf)
{
    // Destinations whose content is not document text.
    static const QSet<QByteArray> skipped{
        "fonttbl", "colortbl", "stylesheet", "info", "pict", "object", "header", "footer", "headerl", "headerr",
        "footerl", "footerr", "footnote", "listtable", "listoverridetable", "rsidtbl", "generator", "themedata",
        "datastore", "latentstyles", "xmlnstbl", "mmathPr", "filetbl", "revtbl", "fldinst",
    };

    struct State {
        bool skip = false;
        int unicodeSkip = 1;
    };
    QList<State> stack{State{}};
    QString text;
    QByteArray pending; // raw \'hh bytes, decoded as Windows-1252
    int skipChars = 0;

    auto flushPending = [&] {
        if (pending.isEmpty()) return;
        if (!stack.last().skip) text.append(TextCodec::decodeWindows1252(pending));
        pending.clear();
    };

    const qsizetype size = rtf.size();
    qsizetype i = 0;
    while (i < size) {
        const char ch = rtf.at(i);
        if (ch == '{') {
            flushPending();
            stack.append(stack.last());
            ++i;
        } else if (ch == '}') {
            flushPending();
            if (stack.size() > 1) stack.removeLast();
            ++i;
        } else if (ch == '\\') {
            ++i;
            if (i >= size) break;
            const char next = rtf.at(i);
            if (next == '\'') {
                if (i + 2 < size + 1) {
                    bool ok = false;
                    const int value = rtf.mid(i + 1, 2).toInt(&ok, 16);
                    i += 3;
                    if (skipChars > 0) { --skipChars; continue; }
                    if (ok) pending.append(char(value));
                }
                continue;
            }
            flushPending();
            if (!std::isalpha(static_cast<unsigned char>(next))) {
                // Control symbol: \\ \{ \} \~ \- \* and others.
                if (next == '*') stack.last().skip = true;
                else if (!stack.last().skip && (next == '\\' || next == '{' || next == '}')) text.append(QLatin1Char(next));
                else if (!stack.last().skip && next == '~') text.append(QChar(0x00A0));
                ++i;
                continue;
            }
            const qsizetype wordStart = i;
            while (i < size && std::isalpha(static_cast<unsigned char>(rtf.at(i)))) ++i;
            const QByteArray word = rtf.mid(wordStart, i - wordStart);
            const qsizetype numberStart = i;
            if (i < size && (rtf.at(i) == '-' || std::isdigit(static_cast<unsigned char>(rtf.at(i))))) ++i;
            while (i < size && std::isdigit(static_cast<unsigned char>(rtf.at(i)))) ++i;
            const QByteArray numberText = rtf.mid(numberStart, i - numberStart);
            const bool hasNumber = !numberText.isEmpty();
            const int number = numberText.toInt();
            if (i < size && rtf.at(i) == ' ') ++i; // delimiter space belongs to the control word

            State &state = stack.last();
            if (skipped.contains(word)) {
                state.skip = true;
            } else if (word == "uc" && hasNumber) {
                state.unicodeSkip = number;
            } else if (word == "u" && hasNumber) {
                if (!state.skip) text.append(QChar(char16_t(number < 0 ? number + 65536 : number)));
                skipChars = state.unicodeSkip;
            } else if (!state.skip) {
                if (word == "par" || word == "line" || word == "row" || word == "sect" || word == "page") text.append(QLatin1Char('\n'));
                else if (word == "tab" || word == "cell") text.append(QLatin1Char('\t'));
                else if (word == "emdash") text.append(QChar(0x2014));
                else if (word == "endash") text.append(QChar(0x2013));
                else if (word == "bullet") text.append(QChar(0x2022));
                else if (word == "lquote") text.append(QChar(0x2018));
                else if (word == "rquote") text.append(QChar(0x2019));
                else if (word == "ldblquote") text.append(QChar(0x201C));
                else if (word == "rdblquote") text.append(QChar(0x201D));
            }
        } else if (ch == '\r' || ch == '\n') {
            ++i;
        } else {
            if (static_cast<unsigned char>(ch) < 0x80) flushPending();
            if (skipChars > 0) {
                --skipChars;
            } else if (!stack.last().skip) {
                // Bytes above 0x7F written raw are in the document code page, usually Windows-1252.
                if (static_cast<unsigned char>(ch) >= 0x80) pending.append(ch);
                else text.append(QLatin1Char(ch));
            }
            ++i;
        }
    }
    flushPending();
    return text;
}

// MARK: ExtractionCache

ExtractionCache::ExtractionCache(QString directory) : m_directory(std::move(directory)) {}

ExtractionCache &ExtractionCache::shared()
{
    static ExtractionCache cache(AppPaths::cacheDirectory());
    return cache;
}

QString ExtractionCache::entryPath(const QString &sourcePath) const
{
    const QByteArray digest = QCryptographicHash::hash(sourcePath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return m_directory + QLatin1Char('/') + QString::fromLatin1(digest) + QStringLiteral(".txt");
}

std::optional<QString> ExtractionCache::text(const FileCandidate &candidate)
{
    const QString entry = entryPath(candidate.path);
    QFile cached(entry);
    if (cached.open(QIODevice::ReadOnly)) {
        const QJsonObject header = QJsonDocument::fromJson(cached.readLine()).object();
        if (header.value(QStringLiteral("path")).toString() == candidate.path
            && header.value(QStringLiteral("size")).toInteger() == candidate.size
            && header.value(QStringLiteral("modified")).toInteger() == candidate.modifiedMSecs) {
            return QString::fromUtf8(cached.readAll());
        }
        cached.close();
    }

    auto text = DocumentExtractor::extractText(candidate.path);
    if (!text) return std::nullopt;

    const QJsonObject header{
        {QStringLiteral("path"), candidate.path},
        {QStringLiteral("size"), candidate.size},
        {QStringLiteral("modified"), candidate.modifiedMSecs},
    };
    QMutexLocker locker(&m_mutex);
    QDir().mkpath(m_directory);
    QFile output(entry);
    if (output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        output.write(QJsonDocument(header).toJson(QJsonDocument::Compact));
        output.write("\n");
        output.write(text->toUtf8());
    }
    return text;
}

ExtractionCache::CleanupReport ExtractionCache::removeStale()
{
    CleanupReport report;
    QDirIterator it(m_directory, {QStringLiteral("*.txt")}, QDir::Files);
    while (it.hasNext()) {
        const QString entry = it.next();
        QFile file(entry);
        QJsonObject header;
        if (file.open(QIODevice::ReadOnly)) header = QJsonDocument::fromJson(file.readLine()).object();
        file.close();
        const QFileInfo source(header.value(QStringLiteral("path")).toString());
        const bool fresh = source.exists() && source.size() == header.value(QStringLiteral("size")).toInteger()
            && source.lastModified().toMSecsSinceEpoch() == header.value(QStringLiteral("modified")).toInteger();
        if (fresh) {
            ++report.kept;
        } else {
            QFile::remove(entry);
            ++report.removed;
        }
    }
    return report;
}

void ExtractionCache::clear()
{
    QDir(m_directory).removeRecursively();
}

qint64 ExtractionCache::sizeOnDisk() const
{
    return AppPaths::folderSize(m_directory);
}

} // namespace garimpo
