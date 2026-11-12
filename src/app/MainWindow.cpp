#include "MainWindow.h"

#include "AppSettings.h"
#include "FileActions.h"
#include "HighlightDelegate.h"
#include "PreviewView.h"
#include "ReplaceDialog.h"
#include "ResultsModel.h"
#include "SearchForm.h"
#include "SearchTab.h"
#include "SettingsDialog.h"
#include "TextCodec.h"
#include "Theme.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTextDocument>
#include <QThread>
#include <QVBoxLayout>

using namespace garimpo;

namespace {

constexpr qsizetype kMaxPreviewCharacters = 4'000'000;

QString safeFileName(const QString &title)
{
    QString name = title;
    for (const QChar ch : QStringLiteral("/:\\?%*|\"<>")) name.replace(ch, QLatin1Char('_'));
    return name.trimmed().isEmpty() ? QStringLiteral("Results") : name.trimmed();
}

QFrame *separator()
{
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("Garimpo"));
    m_history = History::load();
    m_history.setLimit(AppSettings::historyLimit());

    m_form = new SearchForm(&m_history, this);
    connect(m_form, &SearchForm::searchRequested, this, &MainWindow::search);
    connect(m_form, &SearchForm::stopRequested, this, [this] { stop(); });
    connect(m_form, &SearchForm::replaceRequested, this, &MainWindow::replace);
    connect(m_form, &SearchForm::historyChanged, this, [this] {
        saveHistory();
        m_form->refreshHistory();
    });
    if (m_history.lastQuery) m_form->setQuery(*m_history.lastQuery);
    m_form->setReplacement(m_history.lastReplacement);

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildWelcome());
    m_stack->addWidget(buildResultsArea());

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_form);
    layout->addWidget(separator());
    layout->addWidget(m_stack, 1);
    setCentralWidget(central);

    m_status = new QLabel;
    statusBar()->addWidget(m_status, 1);

    buildMenus();
    resize(1280, 820);
    restoreGeometry(AppSettings::windowGeometry());
    m_splitter->restoreState(AppSettings::splitterState());
    m_preview->setFontSize(AppSettings::previewFontSize());
    if (AppSettings::restoreSession()) restoreSession();
    m_stack->setCurrentIndex(m_tabs.empty() ? 0 : 1);
    m_form->focusPattern();
}

MainWindow::~MainWindow()
{
    m_model->setTab(nullptr);
    for (SearchTab *tab : m_tabs) tab->stop();
}

QWidget *MainWindow::buildWelcome()
{
    auto *label = new QLabel(QStringLiteral(
        "<div style='text-align:center'><h1 style='color:%1'>Garimpo</h1>"
        "<p>Pick or drop a folder, type what to find and press Return.</p>"
        "<p>Leave Find empty to list every file matching the masks.</p>"
        "<p>Expression mode: <code>invoice AND (paid OR \"due date\") NOT draft</code>, <code>/regex/</code> terms allowed.</p>"
        "<p>F3 / Shift+F3 move between matches, Ctrl+Down / Ctrl+Up between files, Return opens the editor.</p>"
        "<p>Folders, patterns and masks you use are remembered in each field's list.</p></div>")
                                 .arg(Theme::accent(palette()).name()));
    label->setAlignment(Qt::AlignCenter);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setTextFormat(Qt::RichText);
    return label;
}

QWidget *MainWindow::buildResultsArea()
{
    m_tabBar = new QTabBar;
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setElideMode(Qt::ElideMiddle);
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::showTab);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabBar, &QTabBar::tabBarDoubleClicked, this, &MainWindow::renameTab);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        SearchTab *tab = m_tabs[size_t(from)];
        m_tabs.erase(m_tabs.begin() + from);
        m_tabs.insert(m_tabs.begin() + to, tab);
    });
    connect(m_tabBar, &QTabBar::customContextMenuRequested, this, &MainWindow::showTabMenu);

    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(QStringLiteral("Filter results by path"));
    m_filter->setClearButtonEnabled(true);
    m_sort = new QComboBox;
    m_sort->addItem(QStringLiteral("Search order"), int(ResultsModel::Sort::SearchOrder));
    m_sort->addItem(QStringLiteral("Path"), int(ResultsModel::Sort::Path));
    m_sort->addItem(QStringLiteral("Match count"), int(ResultsModel::Sort::MatchCount));
    m_sort->addItem(QStringLiteral("Date modified"), int(ResultsModel::Sort::Modified));

    m_model = new ResultsModel(this);
    m_delegate = new HighlightDelegate(m_model, this);
    m_list = new QListView;
    m_list->setModel(m_model);
    m_list->setItemDelegate(m_delegate);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setLayoutMode(QListView::Batched);
    m_list->setBatchSize(500);
    connect(m_list, &QListView::activated, this, [this] { openSelection(); });
    connect(m_list, &QListView::customContextMenuRequested, this, &MainWindow::showResultMenu);
    connect(m_list->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &current) {
        if (SearchTab *tab = currentTab()) tab->selectedRow = current.row();
        updatePreview();
    });
    connect(m_filter, &QLineEdit::textChanged, m_model, &ResultsModel::setFilter);
    connect(m_sort, &QComboBox::currentIndexChanged, this, [this] {
        m_model->setSort(ResultsModel::Sort(m_sort->currentData().toInt()));
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        SearchTab *tab = currentTab();
        if (tab && tab->selectedRow >= 0 && tab->selectedRow < m_model->rowCount()) selectRow(tab->selectedRow);
        else updatePreview();
    });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int first) {
        // Select the first result as soon as it arrives.
        if (first == 0 && !m_list->currentIndex().isValid()) selectRow(m_model->stepMatch(-1, 1));
    });

    auto *filterRowWidget = new QWidget;
    filterRowWidget->setObjectName(QStringLiteral("resultsHeader"));
    filterRowWidget->setAttribute(Qt::WA_StyledBackground);
    auto *filterRow = new QHBoxLayout(filterRowWidget);
    filterRow->setContentsMargins(8, 6, 8, 6);
    filterRow->addWidget(m_filter, 1);
    filterRow->addWidget(m_sort);
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    leftLayout->addWidget(filterRowWidget);
    leftLayout->addWidget(separator());
    leftLayout->addWidget(m_list, 1);

    m_previewTitle = new QLabel(QStringLiteral("Preview"));
    m_previewTitle->setObjectName(QStringLiteral("previewTitle"));
    m_previewTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_previewTitle->setMinimumWidth(80);
    m_previewTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_previewInfo = new QLabel;
    m_previewInfo->setObjectName(QStringLiteral("previewPosition"));
    m_preview = new PreviewView;
    m_preview->setFrameShape(QFrame::NoFrame);
    auto *previewHeaderWidget = new QWidget;
    previewHeaderWidget->setObjectName(QStringLiteral("previewHeader"));
    previewHeaderWidget->setAttribute(Qt::WA_StyledBackground);
    auto *previewHeader = new QHBoxLayout(previewHeaderWidget);
    previewHeader->setContentsMargins(10, 6, 10, 6);
    previewHeader->addWidget(m_previewTitle, 1);
    previewHeader->addWidget(m_previewInfo);
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(previewHeaderWidget);
    rightLayout->addWidget(separator());
    rightLayout->addWidget(m_preview, 1);

    m_splitter = new QSplitter;
    m_splitter->setHandleWidth(7);
    m_splitter->addWidget(left);
    m_splitter->addWidget(right);
    // The occurrences are the focus, so the list gets the larger share.
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({760, 500});
    m_splitter->setChildrenCollapsible(false);
    left->setMinimumWidth(280);
    right->setMinimumWidth(240);

    auto *area = new QWidget;
    auto *layout = new QVBoxLayout(area);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabBar);
    layout->addWidget(m_splitter, 1);
    return area;
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("New Search"), QKeySequence::New, m_form, &SearchForm::focusPattern);
    file->addAction(QStringLiteral("Open Results…"), QKeySequence::Open, this, &MainWindow::openResults);
    file->addSeparator();
    file->addAction(QStringLiteral("Save Results"), QKeySequence::Save, this, [this] { saveResults(false); });
    file->addAction(QStringLiteral("Save Results As…"), QKeySequence::SaveAs, this, [this] { saveResults(true); });
    QMenu *exportMenu = file->addMenu(QStringLiteral("Export"));
    for (ExportFormat format : {ExportFormat::Text, ExportFormat::Csv, ExportFormat::Html})
        exportMenu->addAction(Exporter::title(format) + QStringLiteral("…"), this, [this, format] { exportResults(format); });
    file->addAction(QStringLiteral("Print Results…"), QKeySequence::Print, this, &MainWindow::printResults);
    file->addSeparator();
    file->addAction(QStringLiteral("Close Tab"), QKeySequence::Close, this, [this] {
        if (m_tabBar->currentIndex() >= 0) closeTab(m_tabBar->currentIndex());
    });
    QAction *settings = file->addAction(QStringLiteral("Settings…"), QKeySequence::Preferences, this, &MainWindow::showSettings);
    settings->setMenuRole(QAction::PreferencesRole);
    QAction *quit = file->addAction(QStringLiteral("Quit"), QKeySequence::Quit, this, &QWidget::close);
    quit->setMenuRole(QAction::QuitRole);

    QMenu *searchMenu = menuBar()->addMenu(QStringLiteral("&Search"));
    searchMenu->addAction(QStringLiteral("Search"), QKeySequence(Qt::CTRL | Qt::Key_Return), this, &MainWindow::search);
    searchMenu->addAction(QStringLiteral("Search Again"), QKeySequence::Refresh, this, [this] { searchAgain(); });
    searchMenu->addAction(QStringLiteral("Stop"), QKeySequence(Qt::Key_Escape), this, [this] { stop(); });
    searchMenu->addSeparator();
    searchMenu->addAction(QStringLiteral("Replace…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F), this, &MainWindow::replace);
    searchMenu->addAction(QStringLiteral("Undo Last Replace…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z), this, &MainWindow::undoLastReplace);
    searchMenu->addSeparator();
    searchMenu->addAction(QStringLiteral("Next Match"), QKeySequence::FindNext, this, [this] { stepMatch(1); });
    searchMenu->addAction(QStringLiteral("Previous Match"), QKeySequence::FindPrevious, this, [this] { stepMatch(-1); });
    searchMenu->addAction(QStringLiteral("Next File"), QKeySequence(Qt::CTRL | Qt::Key_Down), this, [this] { stepFile(1); });
    searchMenu->addAction(QStringLiteral("Previous File"), QKeySequence(Qt::CTRL | Qt::Key_Up), this, [this] { stepFile(-1); });
    searchMenu->addSeparator();
    searchMenu->addAction(QStringLiteral("Open in Editor"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::openSelection);
    searchMenu->addAction(QStringLiteral("Show in File Manager"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this, [this] {
        if (const FileResult *file = m_model->fileAt(m_list->currentIndex().row())) FileActions::revealInFileManager(file->path);
    });

    QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(QStringLiteral("Next Tab"), QKeySequence::NextChild, this, [this] {
        if (m_tabBar->count()) m_tabBar->setCurrentIndex((m_tabBar->currentIndex() + 1) % m_tabBar->count());
    });
    view->addAction(QStringLiteral("Previous Tab"), QKeySequence::PreviousChild, this, [this] {
        if (m_tabBar->count()) m_tabBar->setCurrentIndex((m_tabBar->currentIndex() - 1 + m_tabBar->count()) % m_tabBar->count());
    });
    view->addAction(QStringLiteral("Rename Tab…"), this, [this] { renameTab(m_tabBar->currentIndex()); });
    view->addSeparator();
    view->addAction(QStringLiteral("Larger Preview Text"), QKeySequence::ZoomIn, this, [this] {
        AppSettings::setPreviewFontSize(qMin(28, AppSettings::previewFontSize() + 1));
        m_preview->setFontSize(AppSettings::previewFontSize());
    });
    view->addAction(QStringLiteral("Smaller Preview Text"), QKeySequence::ZoomOut, this, [this] {
        AppSettings::setPreviewFontSize(qMax(8, AppSettings::previewFontSize() - 1));
        m_preview->setFontSize(AppSettings::previewFontSize());
    });
}

// MARK: Searching

bool MainWindow::validate(const SearchQuery &query, bool requirePattern)
{
    const QStringList folders = query.directoryList();
    if (folders.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("No folder"), QStringLiteral("Choose at least one folder to search."));
        return false;
    }
    QStringList missing;
    for (const QString &folder : folders)
        if (!QFileInfo::exists(folder)) missing.append(folder);
    if (!missing.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Folder not found"), missing.join(QLatin1Char('\n')));
        return false;
    }
    if (requirePattern && query.pattern.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Nothing to replace"), QStringLiteral("Type the text to find first."));
        return false;
    }
    if (!query.pattern.isEmpty()) {
        try {
            Matcher matcher(query);
        } catch (const PatternError &error) {
            QMessageBox::warning(this, QStringLiteral("Invalid pattern"), error.message());
            return false;
        }
    }
    return true;
}

void MainWindow::rememberForm(bool includeReplacement)
{
    m_history.remember(m_form->query());
    if (includeReplacement) m_history.remember(m_form->replacement(), HistoryField::Replacements);
    m_history.lastReplacement = m_form->replacement();
    saveHistory();
    m_form->refreshHistory();
}

void MainWindow::search()
{
    const SearchQuery query = m_form->query();
    if (!validate(query, false)) return;
    rememberForm(false);

    SearchTab *tab = currentTab();
    if (AppSettings::openInNewTab() || !tab) {
        tab = new SearchTab(query, this);
        addTab(tab);
    } else {
        tab->setQuery(query);
    }
    tab->start(AppSettings::limits());
    m_list->setFocus();
}

void MainWindow::searchAgain(SearchTab *tab)
{
    if (!tab) tab = currentTab();
    if (tab) tab->start(AppSettings::limits());
}

void MainWindow::stop()
{
    if (SearchTab *tab = currentTab()) tab->stop();
}

void MainWindow::saveHistory()
{
    m_history.save();
}

// MARK: Tabs

SearchTab *MainWindow::currentTab() const
{
    const int index = m_tabBar->currentIndex();
    return index >= 0 && index < int(m_tabs.size()) ? m_tabs[size_t(index)] : nullptr;
}

int MainWindow::indexOf(SearchTab *tab) const
{
    auto it = std::find(m_tabs.begin(), m_tabs.end(), tab);
    return it == m_tabs.end() ? -1 : int(it - m_tabs.begin());
}

void MainWindow::addTab(SearchTab *tab)
{
    m_tabs.push_back(tab);
    connect(tab, &SearchTab::titleChanged, this, [this, tab] { updateTabTitle(tab); });
    connect(tab, &SearchTab::stateChanged, this, [this, tab] {
        updateTabTitle(tab);
        if (tab == currentTab()) {
            m_form->setRunning(tab->isRunning());
            updateStatus();
        }
        if (tab->state() == SearchTab::State::Finished || tab->state() == SearchTab::State::Cancelled) saveSession();
    });
    const int index = m_tabBar->addTab(tab->title());
    updateTabTitle(tab);
    m_stack->setCurrentIndex(1);
    m_tabBar->setCurrentIndex(index);
}

void MainWindow::updateTabTitle(SearchTab *tab)
{
    const int index = indexOf(tab);
    if (index < 0) return;
    QString text = tab->title();
    if (tab->state() != SearchTab::State::Idle)
        text += QStringLiteral("  %1").arg(QLocale().toString(qint64(tab->files().size())));
    if (tab->isRunning()) text.prepend(QStringLiteral("⏳ "));
    if (tab->state() == SearchTab::State::Failed) text.prepend(QStringLiteral("⚠ "));
    m_tabBar->setTabText(index, text);
    m_tabBar->setTabToolTip(index, tab->query().directories);
}

void MainWindow::showTab(int index)
{
    SearchTab *tab = index >= 0 && index < int(m_tabs.size()) ? m_tabs[size_t(index)] : nullptr;
    m_delegate->setRoots(tab ? tab->query().directoryList() : QStringList());
    m_model->setTab(tab);
    m_form->setRunning(tab && tab->isRunning());
    updateStatus();
}

void MainWindow::closeTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size())) return;
    SearchTab *tab = m_tabs[size_t(index)];
    if (m_model->tab() == tab) m_model->setTab(nullptr);
    m_tabs.erase(m_tabs.begin() + index);
    m_tabBar->removeTab(index);
    tab->stop();
    tab->deleteLater();
    if (m_tabs.empty()) {
        m_stack->setCurrentIndex(0);
        m_preview->showMessage(QString());
        m_previewTitle->setText(QStringLiteral("Preview"));
        m_previewInfo->clear();
        updateStatus();
    }
    saveSession();
}

void MainWindow::closeOtherTabs(int index)
{
    SearchTab *keep = m_tabs[size_t(index)];
    for (int i = int(m_tabs.size()) - 1; i >= 0; --i)
        if (m_tabs[size_t(i)] != keep) closeTab(i);
}

void MainWindow::renameTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size())) return;
    SearchTab *tab = m_tabs[size_t(index)];
    bool ok = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("Rename Tab"),
                                                QStringLiteral("Title (leave empty to use the search pattern):"),
                                                QLineEdit::Normal, tab->title(), &ok);
    if (!ok) return;
    tab->setTitle(title);
    saveSession();
}

void MainWindow::showTabMenu(const QPoint &position)
{
    const int index = m_tabBar->tabAt(position);
    if (index < 0) return;
    SearchTab *tab = m_tabs[size_t(index)];
    QMenu menu(this);
    menu.addAction(QStringLiteral("Rename…"), this, [this, index] { renameTab(index); });
    menu.addAction(QStringLiteral("Search Again"), this, [this, tab] { searchAgain(tab); });
    menu.addAction(QStringLiteral("Load Query into Form"), this, [this, tab] {
        m_form->setQuery(tab->query());
        m_form->focusPattern();
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Save…"), this, [this, index] {
        m_tabBar->setCurrentIndex(index);
        saveResults(false);
    });
    if (!tab->filePath().isEmpty())
        menu.addAction(QStringLiteral("Show Saved File"), this, [tab] { FileActions::revealInFileManager(tab->filePath()); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Close Tab"), this, [this, index] { closeTab(index); });
    QAction *others = menu.addAction(QStringLiteral("Close Other Tabs"), this, [this, index] { closeOtherTabs(index); });
    others->setEnabled(m_tabs.size() > 1);
    menu.exec(m_tabBar->mapToGlobal(position));
}

// MARK: Results and preview

void MainWindow::selectRow(int row)
{
    if (row < 0 || row >= m_model->rowCount()) return;
    const QModelIndex index = m_model->index(row);
    m_list->setCurrentIndex(index);
    m_list->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

void MainWindow::stepMatch(int step)
{
    selectRow(m_model->stepMatch(m_list->currentIndex().row(), step));
}

void MainWindow::stepFile(int step)
{
    selectRow(m_model->stepFile(m_list->currentIndex().row(), step));
}

void MainWindow::updatePreview()
{
    const int row = m_list->currentIndex().row();
    const FileResult *file = m_model->fileAt(row);
    if (!file) {
        m_previewTitle->setText(QStringLiteral("Preview"));
        m_previewInfo->clear();
        m_preview->showMessage(QString());
        return;
    }
    const LineMatch *match = m_model->matchAt(row);
    QString folder = file->directory();
    if (folder.startsWith(QDir::homePath())) folder = QLatin1Char('~') + folder.mid(QDir::homePath().size());
    m_previewTitle->setToolTip(file->path);
    m_previewTitle->setText(QStringLiteral("%1  <span style='font-weight:normal; color:%2'>%3</span>")
                                .arg(file->name().toHtmlEscaped(), Theme::dimText(palette()).name(QColor::HexArgb),
                                     folder.toHtmlEscaped()));
    QString info = match ? QStringLiteral("Line %1, col %2").arg(match->lineNumber).arg(match->column()) : QString();
    if (!file->encoding.isEmpty()) info += (info.isEmpty() ? QString() : QStringLiteral("  ·  ")) + file->encoding;
    m_previewInfo->setText(info);

    if (m_preview->shownPath() == file->path) {
        m_preview->selectMatch(match);
        return;
    }

    // Read the file off the UI thread; only the newest request is shown.
    const quint64 generation = ++m_previewGeneration;
    const FileResult copy = *file;
    QPointer<MainWindow> self(this);
    QThread *thread = QThread::create([self, generation, copy] {
        QString text;
        bool ok = false;
        if (copy.kind == FileKind::Document) {
            const auto extracted = ExtractionCache::shared().text({copy.path, copy.size, copy.modifiedMSecs});
            ok = extracted.has_value();
            if (ok) text = *extracted;
        } else if (const auto decoded = TextCodec::decodeFile(copy.path, 0)) {
            ok = true;
            text = decoded->text;
        }
        if (text.size() > kMaxPreviewCharacters) text.truncate(kMaxPreviewCharacters);
        QMetaObject::invokeMethod(self.data(), [self, generation, copy, text, ok] {
            if (!self || generation != self->m_previewGeneration) return;
            if (!ok) {
                self->m_preview->showMessage(QStringLiteral("This file cannot be previewed."));
                return;
            }
            self->m_preview->showText(copy.path, text, copy);
            self->m_preview->selectMatch(self->m_model->matchAt(self->m_list->currentIndex().row()));
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::openSelection()
{
    const int row = m_list->currentIndex().row();
    const FileResult *file = m_model->fileAt(row);
    if (!file) return;
    if (file->kind == FileKind::Document) {
        FileActions::openWithDefaultApp(file->path);
        return;
    }
    const LineMatch *match = m_model->matchAt(row);
    FileActions::openInEditor(file->path, match ? match->lineNumber : 1, match ? match->column() : 1);
}

void MainWindow::showResultMenu(const QPoint &position)
{
    const QModelIndex index = m_list->indexAt(position);
    if (!index.isValid()) return;
    m_list->setCurrentIndex(index);
    const FileResult *file = m_model->fileAt(index.row());
    const LineMatch *match = m_model->matchAt(index.row());
    if (!file) return;
    const QString path = file->path;
    const QString name = file->name();
    const QString line = match ? match->text : QString();
    const int fileIndex = m_model->rowAt(index.row()).file;

    QMenu menu(this);
    menu.addAction(QStringLiteral("Open in Editor"), this, &MainWindow::openSelection);
    menu.addAction(QStringLiteral("Open with Default App"), this, [path] { FileActions::openWithDefaultApp(path); });
    menu.addAction(QStringLiteral("Show in File Manager"), this, [path] { FileActions::revealInFileManager(path); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Copy Path"), this, [path] { FileActions::copyToClipboard(path); });
    menu.addAction(QStringLiteral("Copy Name"), this, [name] { FileActions::copyToClipboard(name); });
    if (match) menu.addAction(QStringLiteral("Copy Line"), this, [line] { FileActions::copyToClipboard(line); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Remove from Results"), this, [this, fileIndex] {
        if (SearchTab *tab = currentTab()) tab->removeFile(fileIndex);
    });
    menu.exec(m_list->viewport()->mapToGlobal(position));
}

void MainWindow::updateStatus()
{
    SearchTab *tab = currentTab();
    if (!tab) {
        m_status->clear();
        return;
    }
    const SearchStats &stats = tab->stats();
    const QLocale locale;
    const QString found = QStringLiteral("%1 matches in %2 files").arg(locale.toString(stats.matchCount), locale.toString(qint64(tab->files().size())));
    const QString scanned = QStringLiteral("%1 scanned").arg(locale.toString(stats.filesScanned));
    QStringList skipped;
    if (stats.skippedBinary) skipped.append(QStringLiteral("%1 binary").arg(locale.toString(stats.skippedBinary)));
    if (stats.skippedUnreadable) skipped.append(QStringLiteral("%1 unreadable").arg(locale.toString(stats.skippedUnreadable)));
    if (stats.skippedDocuments) skipped.append(QStringLiteral("%1 documents").arg(locale.toString(stats.skippedDocuments)));
    const QString skippedText = skipped.isEmpty() ? QString() : QStringLiteral(", skipped ") + skipped.join(QStringLiteral(", "));
    const QString limit = stats.limitReached ? QStringLiteral(" · result limit reached") : QString();

    switch (tab->state()) {
    case SearchTab::State::Running:
        m_status->setText(QStringLiteral("Searching… %1 · %2").arg(found, scanned));
        break;
    case SearchTab::State::Cancelled:
        m_status->setText(QStringLiteral("Stopped · %1 · %2%3").arg(found, scanned, skippedText));
        break;
    case SearchTab::State::Failed:
        m_status->setText(tab->errorMessage());
        break;
    case SearchTab::State::Finished:
        m_status->setText(QStringLiteral("%1 · %2%3 · %4 s · %5%6")
                              .arg(found, scanned, skippedText, locale.toString(stats.durationSeconds, 'f', 2),
                                   locale.toString(tab->createdAt(), QLocale::ShortFormat), limit));
        break;
    case SearchTab::State::Idle:
        m_status->clear();
        break;
    }
}

// MARK: Files

void MainWindow::openDocuments(const QStringList &paths)
{
    for (const QString &path : paths) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        bool already = false;
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i]->filePath() == absolute) {
                m_tabBar->setCurrentIndex(int(i));
                already = true;
            }
        }
        if (already) continue;
        QString error;
        auto document = ResultDocument::read(absolute, &error);
        if (!document) {
            QMessageBox::warning(this, QStringLiteral("Cannot open"), error);
            continue;
        }
        addTab(new SearchTab(std::move(*document), absolute, this));
    }
    saveSession();
}

void MainWindow::openResults()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, QStringLiteral("Open Results"), QString(),
                                                            QStringLiteral("Garimpo results (*.garimpo)"));
    if (!paths.isEmpty()) openDocuments(paths);
}

void MainWindow::saveResults(bool saveAs)
{
    SearchTab *tab = currentTab();
    if (!tab) return;
    QString path = tab->filePath();
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Results"),
                                            safeFileName(tab->title()) + QStringLiteral(".garimpo"),
                                            QStringLiteral("Garimpo results (*.garimpo)"));
        if (path.isEmpty()) return;
        if (!path.endsWith(QStringLiteral(".garimpo"))) path += QStringLiteral(".garimpo");
    }
    QString error;
    if (!tab->document().write(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Cannot save"), error);
        return;
    }
    tab->setFilePath(path);
    tab->setDirty(false);
}

void MainWindow::exportResults(ExportFormat format)
{
    SearchTab *tab = currentTab();
    if (!tab) return;
    const QString extension = Exporter::extension(format);
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Results"),
                                                      safeFileName(tab->title()) + QLatin1Char('.') + extension,
                                                      QStringLiteral("%1 (*.%2)").arg(Exporter::title(format), extension));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(Exporter::render(tab->document(), format).toUtf8()) < 0 || !file.commit())
        QMessageBox::warning(this, QStringLiteral("Cannot export"), QStringLiteral("Cannot write %1").arg(path));
}

void MainWindow::printResults()
{
    SearchTab *tab = currentTab();
    if (!tab) return;
    QPrinter printer;
    printer.setDocName(tab->title());
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QTextDocument document;
    document.setHtml(Exporter::html(tab->document()));
    document.print(&printer);
}

void MainWindow::saveSession()
{
    if (!AppSettings::restoreSession()) {
        QFile::remove(AppPaths::sessionFile());
        return;
    }
    QJsonArray tabs;
    int selected = -1;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->isRunning()) continue;
        if (m_tabs[i] == currentTab()) selected = int(tabs.size());
        QJsonObject object = m_tabs[i]->document().toJson();
        if (!m_tabs[i]->filePath().isEmpty()) object.insert(QStringLiteral("filePath"), m_tabs[i]->filePath());
        tabs.append(object);
    }
    Json::writeObject(AppPaths::sessionFile(), {{QStringLiteral("tabs"), tabs}, {QStringLiteral("selected"), selected}}, true);
}

void MainWindow::restoreSession()
{
    const auto session = Json::readObject(AppPaths::sessionFile());
    if (!session) return;
    for (const QJsonValue &value : session->value(QStringLiteral("tabs")).toArray()) {
        const QJsonObject object = value.toObject();
        addTab(new SearchTab(ResultDocument::fromJson(object), object.value(QStringLiteral("filePath")).toString(), this));
    }
    const int selected = session->contains(QStringLiteral("selected")) ? session->value(QStringLiteral("selected")).toInt(-1)
                                                                       : session->value(QStringLiteral("selectedIndex")).toInt(-1);
    if (selected >= 0 && selected < m_tabBar->count()) m_tabBar->setCurrentIndex(selected);
}

// MARK: Replace

void MainWindow::replace()
{
    const SearchQuery query = m_form->query();
    if (!validate(query, true)) return;
    if (query.mode == PatternMode::Expression || query.fileNamesOnly) {
        QMessageBox::information(this, QStringLiteral("Replace not available"),
                                 QStringLiteral("Replace works with Text or Regex patterns on file contents."));
        return;
    }
    rememberForm(true);
    ReplaceDialog dialog(query, m_form->replacement(), this);
    if (dialog.exec() != QDialog::Accepted) return;

    const ReplaceOutcome outcome = Replacer::execute(dialog.selectedPlan());
    QString message = outcome.batch
        ? QStringLiteral("Changed %1 lines in %2 files.\nUse Search ▸ Undo Last Replace to revert.")
              .arg(outcome.batch->changedLines()).arg(outcome.batch->entries.size())
        : QStringLiteral("No file was changed.");
    if (!outcome.failures.empty()) {
        message += QStringLiteral("\n\nNot changed:");
        for (const Problem &failure : outcome.failures) message += QStringLiteral("\n%1: %2").arg(failure.path, failure.reason);
    }
    QMessageBox::information(this, QStringLiteral("Replace finished"), message);
    if (SearchTab *tab = currentTab(); tab && tab->query().directories == query.directories) searchAgain(tab);
}

void MainWindow::undoLastReplace()
{
    const auto batch = Replacer::lastBatch();
    if (!batch) {
        QMessageBox::information(this, QStringLiteral("Nothing to undo"), QStringLiteral("There is no replace to revert."));
        return;
    }
    const auto answer = QMessageBox::question(this, QStringLiteral("Undo Replace"),
        QStringLiteral("Undo the replace of “%1” with “%2”?\n\n%3 files changed on %4 will be restored from backup.")
            .arg(batch->pattern, batch->replacement).arg(batch->entries.size())
            .arg(QLocale().toString(batch->date, QLocale::ShortFormat)));
    if (answer != QMessageBox::Yes) return;

    UndoOutcome outcome = Replacer::undo(*batch);
    if (!outcome.modifiedSince.isEmpty()) {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Files edited after the replace"),
                        outcome.modifiedSince.join(QLatin1Char('\n')) + QStringLiteral("\n\nRestoring them discards those edits."),
                        QMessageBox::NoButton, this);
        box.addButton(QStringLiteral("Keep Edits"), QMessageBox::RejectRole);
        QPushButton *restore = box.addButton(QStringLiteral("Restore Anyway"), QMessageBox::DestructiveRole);
        box.exec();
        if (box.clickedButton() == restore) outcome = Replacer::undo(*batch, true);
    }
    QString message = QStringLiteral("Restored %1 files.").arg(outcome.restored.size());
    if (!outcome.modifiedSince.isEmpty()) message += QStringLiteral("\nKept %1 edited files.").arg(outcome.modifiedSince.size());
    for (const Problem &failure : outcome.failures) message += QStringLiteral("\nFailed %1: %2").arg(failure.path, failure.reason);
    QMessageBox::information(this, QStringLiteral("Undo finished"), message);
    if (SearchTab *tab = currentTab()) searchAgain(tab);
}

void MainWindow::showSettings()
{
    SettingsDialog dialog(&m_history, this);
    connect(&dialog, &SettingsDialog::historyChanged, this, [this] {
        saveHistory();
        m_form->refreshHistory();
    });
    dialog.exec();
    m_preview->setFontSize(AppSettings::previewFontSize());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_history.lastQuery = m_form->query();
    m_history.lastReplacement = m_form->replacement();
    saveHistory();
    saveSession();
    AppSettings::setWindowGeometry(saveGeometry());
    AppSettings::setSplitterState(m_splitter->saveState());
    event->accept();
}

