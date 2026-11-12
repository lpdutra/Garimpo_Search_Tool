#pragma once

#include "Storage.h"

#include <QDialog>

class QComboBox;
class QTreeWidget;

/// Preferences: editor command, behavior, limits, history and stored data.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(garimpo::History *history, QWidget *parent = nullptr);

Q_SIGNALS:
    void historyChanged();

private:
    QWidget *generalPage();
    QWidget *historyPage();
    QWidget *storagePage();
    void reloadHistory();
    garimpo::HistoryField currentField() const;

    garimpo::History *m_history;
    QComboBox *m_field = nullptr;
    QTreeWidget *m_entries = nullptr;
};
