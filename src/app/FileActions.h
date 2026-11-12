#pragma once

#include <QString>
#include <QStringList>

/// Opening files outside the app: external editor, default app, file manager, clipboard.
namespace FileActions {

/// Runs the editor command from Settings with {file}, {line} and {column} replaced; the path is
/// shell-quoted. Falls back to the default app when no command is set or it fails.
void openInEditor(const QString &path, int line = 1, int column = 1);
void openWithDefaultApp(const QString &path);
void revealInFileManager(const QString &path);
void copyToClipboard(const QString &text);
QString shellQuote(const QString &value);

} // namespace FileActions
