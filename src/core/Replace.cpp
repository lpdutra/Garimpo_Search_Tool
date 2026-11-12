#include "Replace.h"

#include "Documents.h"
#include "Matcher.h"
#include "TextCodec.h"
#include "TextLines.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>

#include <algorithm>

namespace garimpo {

namespace {

const QString kManifest = QStringLiteral("manifest.json");

QString hashOf(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool writeAtomically(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(data);
    return file.commit();
}

} // namespace

int FileChange::selectedLineCount() const
{
    if (!included) return 0;
    return int(std::count_if(lines.begin(), lines.end(), [](const LineChange &line) { return line.included; }));
}

int ReplacePlan::selectedLineCount() const
{
    int total = 0;
    for (const FileChange &file : files) total += file.selectedLineCount();
    return total;
}

int ReplacePlan::selectedFileCount() const
{
    return int(std::count_if(files.begin(), files.end(), [](const FileChange &file) { return file.selectedLineCount() > 0; }));
}

int ReplaceBatch::changedLines() const
{
    int total = 0;
    for (const Entry &entry : entries) total += entry.changedLines;
    return total;
}

QJsonObject ReplaceBatch::toJson() const
{
    QJsonArray array;
    for (const Entry &entry : entries) {
        array.append(QJsonObject{
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("backupName"), entry.backupName},
            {QStringLiteral("writtenHash"), entry.writtenHash},
            {QStringLiteral("changedLines"), entry.changedLines},
        });
    }
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("date"), date.toString(Qt::ISODate)},
        {QStringLiteral("pattern"), pattern},
        {QStringLiteral("replacement"), replacement},
        {QStringLiteral("entries"), array},
    };
}

ReplaceBatch ReplaceBatch::fromJson(const QJsonObject &object)
{
    ReplaceBatch batch;
    batch.id = object.value(QStringLiteral("id")).toString();
    batch.date = QDateTime::fromString(object.value(QStringLiteral("date")).toString(), Qt::ISODate);
    batch.pattern = object.value(QStringLiteral("pattern")).toString();
    batch.replacement = object.value(QStringLiteral("replacement")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        batch.entries.push_back({entry.value(QStringLiteral("path")).toString(), entry.value(QStringLiteral("backupName")).toString(),
                                 entry.value(QStringLiteral("writtenHash")).toString(),
                                 entry.value(QStringLiteral("changedLines")).toInt()});
    }
    return batch;
}

QString Replacer::apply(const QRegularExpression &regex, const QString &text, const QString &replacement, bool literal)
{
    QString output;
    qsizetype last = 0;
    auto iterator = regex.globalMatch(text);
    bool changed = false;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        if (match.capturedLength() == 0) continue;
        changed = true;
        output.append(QStringView(text).sliced(last, match.capturedStart() - last));
        last = match.capturedEnd();
        if (literal) {
            output.append(replacement);
            continue;
        }
        for (qsizetype i = 0; i < replacement.size(); ++i) {
            const QChar ch = replacement.at(i);
            if (ch != QLatin1Char('$') || i + 1 >= replacement.size()) {
                output.append(ch);
                continue;
            }
            const QChar next = replacement.at(i + 1);
            if (next == QLatin1Char('$')) {
                output.append(QLatin1Char('$'));
                ++i;
            } else if (next.isDigit()) {
                int group = next.digitValue();
                ++i;
                // Two-digit groups only when that group exists.
                if (i + 1 < replacement.size() && replacement.at(i + 1).isDigit()) {
                    const int twoDigits = group * 10 + replacement.at(i + 1).digitValue();
                    if (twoDigits <= regex.captureCount()) {
                        group = twoDigits;
                        ++i;
                    }
                }
                output.append(match.captured(group));
            } else if (next == QLatin1Char('{')) {
                const qsizetype close = replacement.indexOf(QLatin1Char('}'), i + 2);
                if (close < 0) {
                    output.append(ch);
                    continue;
                }
                output.append(match.captured(replacement.mid(i + 2, close - i - 2)));
                i = close;
            } else {
                output.append(ch);
            }
        }
    }
    if (!changed) return text;
    output.append(QStringView(text).sliced(last));
    return output;
}

ReplacePlan Replacer::plan(const SearchQuery &query, const QString &replacement, const QStringList &paths)
{
    const Matcher matcher(query);
    const QRegularExpression regex = matcher.replacementRegex();
    const bool literal = query.mode == PatternMode::Literal;

    ReplacePlan plan{query, replacement, {}, {}};
    for (const QString &path : paths) {
        if (query.searchDocuments && DocumentExtractor::canExtract(path)) {
            plan.skipped.push_back({path, QStringLiteral("Documents are read-only")});
            continue;
        }
        auto decoded = TextCodec::decodeFile(path, 0);
        if (!decoded) {
            plan.skipped.push_back({path, QStringLiteral("Not a readable text file")});
            continue;
        }
        FileChange change{path, encodingName(decoded->encoding), {}, true};
        const QString &text = decoded->text;
        const auto lines = TextLines::split(text);
        for (size_t index = 0; index < lines.size(); ++index) {
            const QString original = TextLines::content(text, lines[index]).toString();
            QString replaced = apply(regex, original, replacement, literal);
            if (replaced != original) change.lines.push_back({int(index + 1), original, std::move(replaced), true});
        }
        if (!change.lines.empty()) plan.files.push_back(std::move(change));
    }
    return plan;
}

ReplaceOutcome Replacer::execute(const ReplacePlan &plan, const QString &backupsRoot)
{
    const QDateTime now = QDateTime::currentDateTime();
    ReplaceBatch batch{now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")), now, plan.query.pattern, plan.replacement, {}};
    const QString folder = backupsRoot + QLatin1Char('/') + batch.id;
    ReplaceOutcome outcome;

    int index = 0;
    for (const FileChange &file : plan.files) {
        ++index;
        if (file.selectedLineCount() == 0) continue;

        QFile input(file.path);
        if (!input.open(QIODevice::ReadOnly)) {
            outcome.failures.push_back({file.path, QStringLiteral("Cannot read the file")});
            continue;
        }
        const QByteArray original = input.readAll();
        input.close();
        auto decoded = TextCodec::decode(original);
        if (!decoded) {
            outcome.failures.push_back({file.path, QStringLiteral("Not a readable text file")});
            continue;
        }

        const QString &text = decoded->text;
        const auto lines = TextLines::split(text);
        std::vector<QString> newContent(lines.size());
        std::vector<bool> touched(lines.size(), false);
        int changed = 0;
        for (const LineChange &change : file.lines) {
            if (!change.included) continue;
            const size_t position = size_t(change.lineNumber - 1);
            // The file may have changed since the preview; only touch lines that still read the same.
            if (position >= lines.size() || TextLines::content(text, lines[position]) != change.original) continue;
            newContent[position] = change.replaced;
            touched[position] = true;
            ++changed;
        }
        if (changed == 0) {
            outcome.failures.push_back({file.path, QStringLiteral("File changed since the preview")});
            continue;
        }

        QString rewritten;
        rewritten.reserve(text.size());
        for (size_t i = 0; i < lines.size(); ++i) {
            if (touched[i]) rewritten.append(newContent[i]);
            else rewritten.append(TextLines::content(text, lines[i]));
            rewritten.append(QStringView(text).sliced(lines[i].start + lines[i].length, lines[i].terminatorLength));
        }
        const auto encoded = TextCodec::encode(rewritten, decoded->encoding);
        if (!encoded) {
            outcome.failures.push_back({file.path, QStringLiteral("New text cannot be saved as %1").arg(encodingName(decoded->encoding))});
            continue;
        }

        QDir().mkpath(folder);
        const QString backupName = QStringLiteral("%1-%2").arg(index, 4, 10, QLatin1Char('0')).arg(QFileInfo(file.path).fileName());
        if (!writeAtomically(folder + QLatin1Char('/') + backupName, original)) {
            outcome.failures.push_back({file.path, QStringLiteral("Cannot write the backup")});
            continue;
        }
        if (!writeAtomically(file.path, *encoded)) {
            outcome.failures.push_back({file.path, QStringLiteral("Cannot write the file")});
            continue;
        }
        batch.entries.push_back({file.path, backupName, hashOf(*encoded), changed});
    }

    if (!batch.entries.empty()) {
        Json::writeObject(folder + QLatin1Char('/') + kManifest, batch.toJson());
        outcome.batch = std::move(batch);
    }
    return outcome;
}

std::vector<ReplaceBatch> Replacer::batches(const QString &backupsRoot)
{
    std::vector<ReplaceBatch> result;
    const QStringList folders = QDir(backupsRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QString &folder : folders) {
        if (auto object = Json::readObject(backupsRoot + QLatin1Char('/') + folder + QLatin1Char('/') + kManifest))
            result.push_back(ReplaceBatch::fromJson(*object));
    }
    return result;
}

std::optional<ReplaceBatch> Replacer::lastBatch(const QString &backupsRoot)
{
    auto all = batches(backupsRoot);
    if (all.empty()) return std::nullopt;
    return all.front();
}

UndoOutcome Replacer::undo(const ReplaceBatch &batch, bool force, const QString &backupsRoot)
{
    const QString folder = backupsRoot + QLatin1Char('/') + batch.id;
    UndoOutcome outcome;
    for (const ReplaceBatch::Entry &entry : batch.entries) {
        QFile current(entry.path);
        if (!force && current.open(QIODevice::ReadOnly) && hashOf(current.readAll()) != entry.writtenHash) {
            outcome.modifiedSince.append(entry.path);
            continue;
        }
        current.close();
        QFile backup(folder + QLatin1Char('/') + entry.backupName);
        if (!backup.open(QIODevice::ReadOnly) || !writeAtomically(entry.path, backup.readAll())) {
            outcome.failures.push_back({entry.path, QStringLiteral("Cannot restore from backup")});
            continue;
        }
        outcome.restored.append(entry.path);
    }
    if (outcome.modifiedSince.isEmpty() && outcome.failures.empty()) QDir(folder).removeRecursively();
    return outcome;
}

void Replacer::clearBackups(const QString &backupsRoot)
{
    QDir(backupsRoot).removeRecursively();
}

} // namespace garimpo
