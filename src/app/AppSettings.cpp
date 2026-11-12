#include "AppSettings.h"

#include "Storage.h"

#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace AppSettings {

namespace {

QVariant value(const char *key, const QVariant &fallback) { return QSettings().value(QLatin1String(key), fallback); }
void store(const char *key, const QVariant &data) { QSettings().setValue(QLatin1String(key), data); }

} // namespace

QString defaultEditorCommand()
{
    QStringList paths{QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin"),
                      QStringLiteral("/snap/bin")};
    if (!QStandardPaths::findExecutable(QStringLiteral("code")).isEmpty()
        || !QStandardPaths::findExecutable(QStringLiteral("code"), paths).isEmpty())
        return QStringLiteral("code -g {file}:{line}:{column}");
    return {};
}

QString editorCommand() { return value("editorCommand", defaultEditorCommand()).toString(); }
void setEditorCommand(const QString &command) { store("editorCommand", command); }

bool openInNewTab() { return value("openInNewTab", true).toBool(); }
void setOpenInNewTab(bool value) { store("openInNewTab", value); }
bool restoreSession() { return value("restoreSession", true).toBool(); }
void setRestoreSession(bool value) { store("restoreSession", value); }
int historyLimit() { return value("historyLimit", garimpo::History::defaultLimit).toInt(); }
void setHistoryLimit(int value) { store("historyLimit", value); }
int previewFontSize() { return value("previewFontSize", 12).toInt(); }
void setPreviewFontSize(int value) { store("previewFontSize", value); }

garimpo::SearchLimits limits()
{
    garimpo::SearchLimits limits;
    limits.maxLinesPerFile = qMax(1, value("maxLinesPerFile", limits.maxLinesPerFile).toInt());
    limits.maxTotalMatches = qMax<qint64>(1, value("maxTotalMatches", limits.maxTotalMatches).toLongLong());
    limits.maxFileSizeMB = qMax<qint64>(1, value("maxFileSizeMB", limits.maxFileSizeMB).toLongLong());
    return limits;
}

void setLimits(const garimpo::SearchLimits &limits)
{
    store("maxLinesPerFile", limits.maxLinesPerFile);
    store("maxTotalMatches", limits.maxTotalMatches);
    store("maxFileSizeMB", limits.maxFileSizeMB);
}

QByteArray windowGeometry() { return value("windowGeometry", QByteArray()).toByteArray(); }
void setWindowGeometry(const QByteArray &geometry) { store("windowGeometry", geometry); }
QByteArray splitterState() { return value("splitterState", QByteArray()).toByteArray(); }
void setSplitterState(const QByteArray &state) { store("splitterState", state); }

} // namespace AppSettings
