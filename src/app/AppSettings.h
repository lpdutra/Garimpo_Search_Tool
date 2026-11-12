#pragma once

#include "SearchEngine.h"

#include <QString>

/// Preferences stored with QSettings.
namespace AppSettings {

QString editorCommand();
void setEditorCommand(const QString &command);
QString defaultEditorCommand();

bool openInNewTab();
void setOpenInNewTab(bool value);
bool restoreSession();
void setRestoreSession(bool value);
int historyLimit();
void setHistoryLimit(int value);
int previewFontSize();
void setPreviewFontSize(int value);

garimpo::SearchLimits limits();
void setLimits(const garimpo::SearchLimits &limits);

QByteArray windowGeometry();
void setWindowGeometry(const QByteArray &geometry);
QByteArray splitterState();
void setSplitterState(const QByteArray &state);

} // namespace AppSettings
