#include "FileActions.h"

#include "AppSettings.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QUrl>

namespace FileActions {

QString shellQuote(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

void openWithDefaultApp(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void openInEditor(const QString &path, int line, int column)
{
    const QString templateText = AppSettings::editorCommand().trimmed();
    if (templateText.isEmpty()) {
        openWithDefaultApp(path);
        return;
    }
    QString command = templateText;
    command.replace(QStringLiteral("{file}"), shellQuote(path));
    command.replace(QStringLiteral("{line}"), QString::number(qMax(1, line)));
    command.replace(QStringLiteral("{column}"), QString::number(qMax(1, column)));

    auto *process = new QProcess;
    // A login shell picks up the user's PATH, which apps started from the desktop do not get.
    const QString shell = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
    QObject::connect(process, &QProcess::finished, process, [process, path](int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0) openWithDefaultApp(path);
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [process, path](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            openWithDefaultApp(path);
            process->deleteLater();
        }
    });
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    process->start(shell, {QStringLiteral("-lc"), command});
}

void revealInFileManager(const QString &path)
{
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
#else
    // The FileManager1 D-Bus interface selects the file in Nautilus, Dolphin, Nemo and others.
    const bool started = QProcess::startDetached(QStringLiteral("dbus-send"),
        {QStringLiteral("--session"), QStringLiteral("--dest=org.freedesktop.FileManager1"), QStringLiteral("--type=method_call"),
         QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
         QStringLiteral("array:string:") + QUrl::fromLocalFile(path).toString(), QStringLiteral("string:")});
    if (!started) {
        const QFileInfo info(path);
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? path : info.absolutePath()));
    }
#endif
}

void copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

} // namespace FileActions
