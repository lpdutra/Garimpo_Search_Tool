#pragma once

#include "Query.h"
#include "Storage.h"

#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDateEdit;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

/// The search form: pattern, replacement, folders, masks, options and filters.
class SearchForm : public QWidget {
    Q_OBJECT

public:
    explicit SearchForm(garimpo::History *history, QWidget *parent = nullptr);

    garimpo::SearchQuery query() const;
    void setQuery(const garimpo::SearchQuery &query);
    QString replacement() const;
    void setReplacement(const QString &text);
    void setDirectories(const QString &directories);

    /// Reloads every drop-down from the history, keeping what is typed.
    void refreshHistory();
    void focusPattern();
    void setRunning(bool running);

Q_SIGNALS:
    void searchRequested();
    void stopRequested();
    void replaceRequested();
    void historyChanged();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QComboBox *makeCombo(const QString &placeholder, bool monospaced);
    void chooseFolders();
    void updateModeControls();
    void showFolderMenu();

    garimpo::History *m_history;
    QComboBox *m_pattern;
    QComboBox *m_replacement;
    QComboBox *m_directories;
    QComboBox *m_include;
    QComboBox *m_exclude;
    QButtonGroup *m_mode;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_wholeWord;
    QCheckBox *m_recursive;
    QCheckBox *m_hidden;
    QCheckBox *m_documents;
    QCheckBox *m_namesOnly;
    QComboBox *m_scope;
    QPushButton *m_replaceButton;
    QPushButton *m_searchButton;
    QPushButton *m_stopButton;
    QToolButton *m_filtersButton;
    QWidget *m_filters;
    QLineEdit *m_excludeDirectories;
    QSpinBox *m_minSize;
    QSpinBox *m_maxSize;
    QCheckBox *m_afterEnabled;
    QCheckBox *m_beforeEnabled;
    QDateEdit *m_after;
    QDateEdit *m_before;
};
