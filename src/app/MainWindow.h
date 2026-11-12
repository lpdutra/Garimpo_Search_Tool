#pragma once

#include "Export.h"
#include "Storage.h"

#include <QMainWindow>

#include <vector>

class HighlightDelegate;
class PreviewView;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QSplitter;
class QStackedWidget;
class QTabBar;
class ResultsModel;
class SearchForm;
class SearchTab;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();
    ~MainWindow() override;

    void openDocuments(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildMenus();
    QWidget *buildResultsArea();
    QWidget *buildWelcome();

    // Searching
    void search();
    void searchAgain(SearchTab *tab = nullptr);
    void stop();
    bool validate(const garimpo::SearchQuery &query, bool requirePattern);
    void rememberForm(bool includeReplacement);

    // Tabs
    SearchTab *currentTab() const;
    void addTab(SearchTab *tab);
    void closeTab(int index);
    void closeOtherTabs(int index);
    void renameTab(int index);
    void showTab(int index);
    void updateTabTitle(SearchTab *tab);
    int indexOf(SearchTab *tab) const;
    void showTabMenu(const QPoint &position);

    // Results and preview
    void selectRow(int row);
    void updatePreview();
    void openSelection();
    void showResultMenu(const QPoint &position);
    void stepMatch(int step);
    void stepFile(int step);
    void updateStatus();

    // Files
    void openResults();
    void saveResults(bool saveAs);
    void exportResults(garimpo::ExportFormat format);
    void printResults();
    void saveSession();
    void restoreSession();

    // Replace
    void replace();
    void undoLastReplace();

    void showSettings();
    void saveHistory();

    garimpo::History m_history;
    std::vector<SearchTab *> m_tabs;

    SearchForm *m_form;
    QStackedWidget *m_stack;
    QTabBar *m_tabBar;
    QLineEdit *m_filter;
    QComboBox *m_sort;
    QListView *m_list;
    ResultsModel *m_model;
    HighlightDelegate *m_delegate;
    QSplitter *m_splitter;
    QLabel *m_previewTitle;
    QLabel *m_previewInfo;
    PreviewView *m_preview;
    QLabel *m_status;
    quint64 m_previewGeneration = 0;
};
