#include "Storage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace garimpo {

namespace {

const char *fieldKey(HistoryField field)
{
    switch (field) {
    case HistoryField::Directories: return "directories";
    case HistoryField::IncludeMasks: return "includeMasks";
    case HistoryField::ExcludeMasks: return "excludeMasks";
    case HistoryField::Patterns: return "patterns";
    case HistoryField::Replacements: return "replacements";
    }
    return "";
}

constexpr HistoryField kAllFields[] = {HistoryField::Directories, HistoryField::IncludeMasks, HistoryField::ExcludeMasks,
                                       HistoryField::Patterns, HistoryField::Replacements};

} // namespace

// MARK: AppPaths

QString AppPaths::base()
{
    const QString custom = qEnvironmentVariable("GARIMPO_HOME");
    if (!custom.isEmpty()) return custom;
#if defined(Q_OS_MACOS)
    return QDir::homePath() + QStringLiteral("/Library/Application Support/Garimpo");
#else
    QString data = qEnvironmentVariable("XDG_DATA_HOME");
    if (data.isEmpty()) data = QDir::homePath() + QStringLiteral("/.local/share");
    return data + QStringLiteral("/Garimpo");
#endif
}

qint64 AppPaths::folderSize(const QString &directory)
{
    qint64 total = 0;
    QDirIterator it(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// MARK: Json

std::optional<QJsonObject> Json::readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    return document.object();
}

bool Json::writeObject(const QString &path, const QJsonObject &object, bool compact)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(object).toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented));
    return file.commit();
}

// MARK: ResultDocument

QJsonObject ResultDocument::toJson() const
{
    QJsonArray fileArray;
    for (const FileResult &file : files) fileArray.append(file.toJson());
    return QJsonObject{
        {QStringLiteral("version"), currentVersion},
        {QStringLiteral("title"), title},
        {QStringLiteral("query"), query.toJson()},
        {QStringLiteral("createdAt"), createdAt.toString(Qt::ISODate)},
        {QStringLiteral("files"), fileArray},
        {QStringLiteral("stats"), stats.toJson()},
    };
}

ResultDocument ResultDocument::fromJson(const QJsonObject &object)
{
    ResultDocument document;
    document.title = object.value(QStringLiteral("title")).toString();
    document.query = SearchQuery::fromJson(object.value(QStringLiteral("query")).toObject());
    const QDateTime created = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    if (created.isValid()) document.createdAt = created;
    document.stats = SearchStats::fromJson(object.value(QStringLiteral("stats")).toObject());
    const QJsonArray fileArray = object.value(QStringLiteral("files")).toArray();
    document.files.reserve(size_t(fileArray.size()));
    for (const QJsonValue &value : fileArray) document.files.push_back(FileResult::fromJson(value.toObject()));
    if (document.title.isEmpty()) document.title = document.query.summary();
    return document;
}

bool ResultDocument::write(const QString &path, QString *error) const
{
    if (Json::writeObject(path, toJson(), true)) return true;
    if (error) *error = QStringLiteral("Cannot write %1").arg(path);
    return false;
}

std::optional<ResultDocument> ResultDocument::read(const QString &path, QString *error)
{
    const auto object = Json::readObject(path);
    if (!object || !object->contains(QStringLiteral("query"))) {
        if (error) *error = QStringLiteral("%1 is not a Garimpo results file.").arg(QFileInfo(path).fileName());
        return std::nullopt;
    }
    return fromJson(*object);
}

// MARK: History

std::vector<HistoryEntry> History::entries(HistoryField field) const
{
    std::vector<HistoryEntry> list = m_lists.value(int(field));
    std::stable_sort(list.begin(), list.end(), [](const HistoryEntry &a, const HistoryEntry &b) {
        if (a.pinned != b.pinned) return a.pinned;
        return a.lastUsed > b.lastUsed;
    });
    return list;
}

QStringList History::values(HistoryField field) const
{
    QStringList result;
    for (const HistoryEntry &entry : entries(field)) result.append(entry.value);
    return result;
}

void History::remember(const QString &value, HistoryField field)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) return;
    auto &list = m_lists[int(field)];
    auto it = std::find_if(list.begin(), list.end(), [&](const HistoryEntry &entry) { return entry.value == trimmed; });
    if (it != list.end()) it->lastUsed = QDateTime::currentDateTime();
    else list.push_back({trimmed, false, QDateTime::currentDateTime()});
    trim(field);
}

void History::remember(const SearchQuery &query)
{
    remember(query.directories, HistoryField::Directories);
    remember(query.includeMasks, HistoryField::IncludeMasks);
    remember(query.excludeMasks, HistoryField::ExcludeMasks);
    remember(query.pattern, HistoryField::Patterns);
    lastQuery = query;
}

void History::trim(HistoryField field)
{
    // Pinned entries never fall off the list.
    std::vector<HistoryEntry> sorted = entries(field);
    std::vector<HistoryEntry> kept;
    int unpinned = 0;
    const int pinnedCount = int(std::count_if(sorted.begin(), sorted.end(), [](const HistoryEntry &e) { return e.pinned; }));
    for (const HistoryEntry &entry : sorted) {
        if (entry.pinned) kept.push_back(entry);
        else if (unpinned++ < qMax(0, m_limit - pinnedCount)) kept.push_back(entry);
    }
    m_lists[int(field)] = kept;
}

void History::setLimit(int limit)
{
    m_limit = qMax(1, limit);
    for (HistoryField field : kAllFields) trim(field);
}

bool History::isPinned(const QString &value, HistoryField field) const
{
    const auto list = m_lists.value(int(field));
    return std::any_of(list.begin(), list.end(), [&](const HistoryEntry &e) { return e.value == value && e.pinned; });
}

void History::setPinned(bool pinned, const QString &value, HistoryField field)
{
    auto &list = m_lists[int(field)];
    auto it = std::find_if(list.begin(), list.end(), [&](const HistoryEntry &entry) { return entry.value == value; });
    if (it != list.end()) it->pinned = pinned;
    else if (pinned && !value.trimmed().isEmpty()) list.push_back({value.trimmed(), true, QDateTime::currentDateTime()});
}

void History::remove(const QString &value, HistoryField field)
{
    auto &list = m_lists[int(field)];
    list.erase(std::remove_if(list.begin(), list.end(), [&](const HistoryEntry &e) { return e.value == value; }), list.end());
}

void History::clear(HistoryField field)
{
    auto &list = m_lists[int(field)];
    list.erase(std::remove_if(list.begin(), list.end(), [](const HistoryEntry &e) { return !e.pinned; }), list.end());
}

QJsonObject History::toJson() const
{
    QJsonObject lists;
    for (HistoryField field : kAllFields) {
        QJsonArray array;
        for (const HistoryEntry &entry : entries(field)) {
            array.append(QJsonObject{
                {QStringLiteral("value"), entry.value},
                {QStringLiteral("pinned"), entry.pinned},
                {QStringLiteral("lastUsed"), entry.lastUsed.toString(Qt::ISODateWithMs)},
            });
        }
        lists.insert(QLatin1String(fieldKey(field)), array);
    }
    QJsonObject object{{QStringLiteral("lists"), lists}, {QStringLiteral("limit"), m_limit},
                       {QStringLiteral("lastReplacement"), lastReplacement}};
    if (lastQuery) object.insert(QStringLiteral("lastQuery"), lastQuery->toJson());
    return object;
}

History History::fromJson(const QJsonObject &object)
{
    History history;
    history.m_limit = qMax(1, object.value(QStringLiteral("limit")).toInt(defaultLimit));
    const QJsonObject lists = object.value(QStringLiteral("lists")).toObject();
    for (HistoryField field : kAllFields) {
        auto &list = history.m_lists[int(field)];
        for (const QJsonValue &value : lists.value(QLatin1String(fieldKey(field))).toArray()) {
            const QJsonObject entry = value.toObject();
            list.push_back({entry.value(QStringLiteral("value")).toString(), entry.value(QStringLiteral("pinned")).toBool(),
                            QDateTime::fromString(entry.value(QStringLiteral("lastUsed")).toString(), Qt::ISODateWithMs)});
        }
    }
    if (object.contains(QStringLiteral("lastQuery")))
        history.lastQuery = SearchQuery::fromJson(object.value(QStringLiteral("lastQuery")).toObject());
    history.lastReplacement = object.value(QStringLiteral("lastReplacement")).toString();
    return history;
}

History History::load(const QString &path)
{
    const auto object = Json::readObject(path);
    return object ? fromJson(*object) : History();
}

bool History::save(const QString &path) const
{
    return Json::writeObject(path, toJson());
}

} // namespace garimpo
