#include "SearchForm.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>

using namespace garimpo;

namespace {

QLabel *fieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

void setItemsKeepingText(QComboBox *combo, const QStringList &items)
{
    const QString text = combo->currentText();
    combo->blockSignals(true);
    combo->clear();
    combo->addItems(items);
    combo->setCurrentIndex(-1);
    combo->setEditText(text);
    combo->blockSignals(false);
}

} // namespace

SearchForm::SearchForm(History *history, QWidget *parent) : QWidget(parent), m_history(history)
{
    setAcceptDrops(true);

    m_pattern = makeCombo(QStringLiteral("Text to find — leave empty to list files"), true);
    m_replacement = makeCombo(QStringLiteral("Replacement"), true);
    m_directories = makeCombo(QStringLiteral("Folder to search — drop folders here; separate several with ;"), false);
    m_include = makeCombo(QStringLiteral("*.cpp;*.md (empty = all)"), false);
    m_exclude = makeCombo(QStringLiteral("*.min.js;*.log"), false);

    auto *modes = new QWidget;
    auto *modesLayout = new QHBoxLayout(modes);
    modesLayout->setContentsMargins(0, 0, 0, 0);
    modesLayout->setSpacing(0);
    m_mode = new QButtonGroup(this);
    const PatternMode modeList[] = {PatternMode::Literal, PatternMode::Regex, PatternMode::Expression};
    for (PatternMode mode : modeList) {
        auto *button = new QToolButton;
        button->setObjectName(QStringLiteral("modeButton"));
        if (mode == PatternMode::Literal) button->setProperty("position", QStringLiteral("first"));
        if (mode == PatternMode::Expression) button->setProperty("position", QStringLiteral("last"));
        button->setText(patternModeTitle(mode));
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setMinimumWidth(78);
        m_mode->addButton(button, int(mode));
        modesLayout->addWidget(button);
    }
    m_mode->button(0)->setChecked(true);
    m_mode->button(0)->setToolTip(QStringLiteral("Match the text literally"));
    m_mode->button(1)->setToolTip(QStringLiteral("Regular expression (PCRE)"));
    m_mode->button(2)->setToolTip(QStringLiteral("Combine terms: invoice AND (paid OR \"due date\") NOT draft, /regex/"));
    connect(m_mode, &QButtonGroup::idClicked, this, &SearchForm::updateModeControls);

    m_replaceButton = new QPushButton(QStringLiteral("Replace…"));
    connect(m_replaceButton, &QPushButton::clicked, this, &SearchForm::replaceRequested);

    auto *folderButton = new QToolButton;
    folderButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    folderButton->setToolTip(QStringLiteral("Choose folders, favorites and history"));
    folderButton->setPopupMode(QToolButton::InstantPopup);
    connect(folderButton, &QToolButton::clicked, this, &SearchForm::showFolderMenu);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    grid->addWidget(fieldLabel(QStringLiteral("Find")), 0, 0);
    grid->addWidget(m_pattern, 0, 1);
    grid->addWidget(modes, 0, 2);
    grid->addWidget(fieldLabel(QStringLiteral("Replace")), 1, 0);
    grid->addWidget(m_replacement, 1, 1);
    grid->addWidget(m_replaceButton, 1, 2, Qt::AlignLeft);
    grid->addWidget(fieldLabel(QStringLiteral("Folders")), 2, 0);
    grid->addWidget(m_directories, 2, 1);
    grid->addWidget(folderButton, 2, 2, Qt::AlignLeft);
    auto *masks = new QHBoxLayout;
    masks->addWidget(m_include, 1);
    masks->addWidget(new QLabel(QStringLiteral("Exclude")));
    masks->addWidget(m_exclude, 1);
    grid->addWidget(fieldLabel(QStringLiteral("Files")), 3, 0);
    grid->addLayout(masks, 3, 1);

    m_caseSensitive = new QCheckBox(QStringLiteral("Match case"));
    m_wholeWord = new QCheckBox(QStringLiteral("Whole word"));
    m_recursive = new QCheckBox(QStringLiteral("Subfolders"));
    m_hidden = new QCheckBox(QStringLiteral("Hidden files"));
    m_documents = new QCheckBox(QStringLiteral("Documents"));
    m_documents->setToolTip(QStringLiteral("Also search inside PDF, DOCX, ODT and RTF files (PDF needs pdftotext from poppler)"));
    m_namesOnly = new QCheckBox(QStringLiteral("File names only"));
    m_scope = new QComboBox;
    m_scope->addItem(QStringLiteral("Terms on the same line"), int(ExpressionScope::Line));
    m_scope->addItem(QStringLiteral("Terms in the same file"), int(ExpressionScope::File));
    connect(m_namesOnly, &QCheckBox::toggled, this, &SearchForm::updateModeControls);

    m_filtersButton = new QToolButton;
    m_filtersButton->setObjectName(QStringLiteral("filtersButton"));
    m_filtersButton->setText(QStringLiteral("Filters"));
    m_filtersButton->setCheckable(true);
    m_filtersButton->setAutoRaise(true);
    m_filtersButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_filtersButton->setArrowType(Qt::RightArrow);
    connect(m_filtersButton, &QToolButton::toggled, this, [this](bool on) {
        m_filters->setVisible(on);
        m_filtersButton->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });

    m_stopButton = new QPushButton(QStringLiteral("Stop"));
    m_stopButton->setVisible(false);
    connect(m_stopButton, &QPushButton::clicked, this, &SearchForm::stopRequested);
    m_searchButton = new QPushButton(QStringLiteral("Search"));
    m_searchButton->setDefault(true);
    connect(m_searchButton, &QPushButton::clicked, this, &SearchForm::searchRequested);

    auto *options = new QHBoxLayout;
    options->setSpacing(14);
    for (QWidget *widget : {static_cast<QWidget *>(m_caseSensitive), static_cast<QWidget *>(m_wholeWord),
                            static_cast<QWidget *>(m_recursive), static_cast<QWidget *>(m_hidden),
                            static_cast<QWidget *>(m_documents), static_cast<QWidget *>(m_namesOnly),
                            static_cast<QWidget *>(m_scope)})
        options->addWidget(widget);
    options->addStretch();
    options->addWidget(m_filtersButton);
    options->addWidget(m_stopButton);
    options->addWidget(m_searchButton);

    // Filters, hidden until asked for.
    m_filters = new QWidget;
    m_filters->setVisible(false);
    m_excludeDirectories = new QLineEdit;
    m_excludeDirectories->setPlaceholderText(QStringLiteral("Folder names to skip"));
    m_excludeDirectories->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_minSize = new QSpinBox;
    m_maxSize = new QSpinBox;
    for (QSpinBox *box : {m_minSize, m_maxSize}) {
        box->setRange(0, 100000000);
        box->setSuffix(QStringLiteral(" KB"));
        box->setSpecialValueText(QStringLiteral("any"));
        box->setMinimumWidth(110);
    }
    m_afterEnabled = new QCheckBox(QStringLiteral("after"));
    m_beforeEnabled = new QCheckBox(QStringLiteral("before"));
    m_after = new QDateEdit(QDate::currentDate());
    m_before = new QDateEdit(QDate::currentDate());
    for (QDateEdit *edit : {m_after, m_before}) {
        edit->setCalendarPopup(true);
        edit->setEnabled(false);
    }
    connect(m_afterEnabled, &QCheckBox::toggled, m_after, &QWidget::setEnabled);
    connect(m_beforeEnabled, &QCheckBox::toggled, m_before, &QWidget::setEnabled);
    auto *reset = new QPushButton(QStringLiteral("Reset Filters"));
    connect(reset, &QPushButton::clicked, this, [this] {
        m_excludeDirectories->setText(SearchQuery::defaultExcludeDirectories());
        m_minSize->setValue(0);
        m_maxSize->setValue(0);
        m_afterEnabled->setChecked(false);
        m_beforeEnabled->setChecked(false);
    });

    auto *filters = new QGridLayout(m_filters);
    filters->setContentsMargins(0, 4, 0, 0);
    filters->setColumnStretch(1, 1);
    filters->addWidget(fieldLabel(QStringLiteral("Skip folders")), 0, 0);
    filters->addWidget(m_excludeDirectories, 0, 1, 1, 2);
    auto *sizes = new QHBoxLayout;
    sizes->addWidget(m_minSize);
    sizes->addWidget(new QLabel(QStringLiteral("to")));
    sizes->addWidget(m_maxSize);
    sizes->addSpacing(24);
    sizes->addWidget(new QLabel(QStringLiteral("Modified")));
    sizes->addWidget(m_afterEnabled);
    sizes->addWidget(m_after);
    sizes->addWidget(m_beforeEnabled);
    sizes->addWidget(m_before);
    sizes->addStretch();
    sizes->addWidget(reset);
    filters->addWidget(fieldLabel(QStringLiteral("Size")), 1, 0);
    filters->addLayout(sizes, 1, 1, 1, 2);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);
    layout->addLayout(grid);
    layout->addLayout(options);
    layout->addWidget(m_filters);

    for (QComboBox *combo : {m_pattern, m_directories, m_include, m_exclude})
        connect(combo->lineEdit(), &QLineEdit::returnPressed, this, &SearchForm::searchRequested);
    connect(m_replacement->lineEdit(), &QLineEdit::returnPressed, this, &SearchForm::replaceRequested);

    refreshHistory();
    setQuery(SearchQuery());
}

QComboBox *SearchForm::makeCombo(const QString &placeholder, bool monospaced)
{
    auto *combo = new QComboBox;
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setMaxVisibleItems(15);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(20);
    combo->lineEdit()->setPlaceholderText(placeholder);
    combo->lineEdit()->setClearButtonEnabled(true);
    combo->completer()->setCaseSensitivity(Qt::CaseSensitive);
    combo->completer()->setCompletionMode(QCompleter::InlineCompletion);
    if (monospaced) combo->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return combo;
}

void SearchForm::refreshHistory()
{
    setItemsKeepingText(m_pattern, m_history->values(HistoryField::Patterns));
    setItemsKeepingText(m_replacement, m_history->values(HistoryField::Replacements));
    setItemsKeepingText(m_directories, m_history->values(HistoryField::Directories));
    setItemsKeepingText(m_include, m_history->values(HistoryField::IncludeMasks));
    setItemsKeepingText(m_exclude, m_history->values(HistoryField::ExcludeMasks));
}

SearchQuery SearchForm::query() const
{
    SearchQuery query;
    query.pattern = m_pattern->currentText();
    query.mode = PatternMode(m_mode->checkedId());
    query.directories = m_directories->currentText().trimmed();
    query.includeMasks = m_include->currentText().trimmed();
    query.excludeMasks = m_exclude->currentText().trimmed();
    query.excludeDirectories = m_excludeDirectories->text().trimmed();
    query.caseSensitive = m_caseSensitive->isChecked();
    query.wholeWord = m_wholeWord->isChecked();
    query.recursive = m_recursive->isChecked();
    query.includeHidden = m_hidden->isChecked();
    query.searchDocuments = m_documents->isChecked();
    query.fileNamesOnly = m_namesOnly->isChecked();
    query.expressionScope = ExpressionScope(m_scope->currentData().toInt());
    if (m_minSize->value() > 0) query.minSizeKB = m_minSize->value();
    if (m_maxSize->value() > 0) query.maxSizeKB = m_maxSize->value();
    if (m_afterEnabled->isChecked()) query.modifiedAfter = m_after->date();
    if (m_beforeEnabled->isChecked()) query.modifiedBefore = m_before->date();
    return query;
}

void SearchForm::setQuery(const SearchQuery &query)
{
    m_pattern->setEditText(query.pattern);
    m_mode->button(int(query.mode))->setChecked(true);
    m_directories->setEditText(query.directories);
    m_include->setEditText(query.includeMasks);
    m_exclude->setEditText(query.excludeMasks);
    m_excludeDirectories->setText(query.excludeDirectories);
    m_caseSensitive->setChecked(query.caseSensitive);
    m_wholeWord->setChecked(query.wholeWord);
    m_recursive->setChecked(query.recursive);
    m_hidden->setChecked(query.includeHidden);
    m_documents->setChecked(query.searchDocuments);
    m_namesOnly->setChecked(query.fileNamesOnly);
    m_scope->setCurrentIndex(query.expressionScope == ExpressionScope::File ? 1 : 0);
    m_minSize->setValue(int(query.minSizeKB.value_or(0)));
    m_maxSize->setValue(int(query.maxSizeKB.value_or(0)));
    m_afterEnabled->setChecked(query.modifiedAfter.has_value());
    m_beforeEnabled->setChecked(query.modifiedBefore.has_value());
    if (query.modifiedAfter) m_after->setDate(*query.modifiedAfter);
    if (query.modifiedBefore) m_before->setDate(*query.modifiedBefore);
    const bool filtered = query.minSizeKB || query.maxSizeKB || query.modifiedAfter || query.modifiedBefore
        || query.excludeDirectories != SearchQuery::defaultExcludeDirectories();
    m_filtersButton->setChecked(filtered);
    updateModeControls();
}

QString SearchForm::replacement() const { return m_replacement->currentText(); }
void SearchForm::setReplacement(const QString &text) { m_replacement->setEditText(text); }
void SearchForm::setDirectories(const QString &directories) { m_directories->setEditText(directories); }

void SearchForm::focusPattern()
{
    m_pattern->setFocus();
    m_pattern->lineEdit()->selectAll();
}

void SearchForm::setRunning(bool running)
{
    m_stopButton->setVisible(running);
}

void SearchForm::updateModeControls()
{
    const auto mode = PatternMode(m_mode->checkedId());
    m_scope->setVisible(mode == PatternMode::Expression);
    const bool canReplace = mode != PatternMode::Expression && !m_namesOnly->isChecked();
    m_replaceButton->setEnabled(canReplace);
    m_replacement->setEnabled(canReplace);
    m_replacement->lineEdit()->setPlaceholderText(mode == PatternMode::Regex ? QStringLiteral("Replacement ($1, $2 for groups)")
                                                                              : QStringLiteral("Replacement"));
    switch (mode) {
    case PatternMode::Literal: m_pattern->lineEdit()->setPlaceholderText(QStringLiteral("Text to find — leave empty to list files")); break;
    case PatternMode::Regex: m_pattern->lineEdit()->setPlaceholderText(QStringLiteral("Regular expression, e.g. void\\s+(\\w+)\\(")); break;
    case PatternMode::Expression: m_pattern->lineEdit()->setPlaceholderText(QStringLiteral("e.g. invoice AND (paid OR \"due date\") NOT draft")); break;
    }
}

void SearchForm::chooseFolders()
{
    const QString start = query().directoryList().value(0);
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose Folder"), start);
    if (!folder.isEmpty()) m_directories->setEditText(folder);
}

void SearchForm::showFolderMenu()
{
    QMenu menu(this);
    menu.addAction(QStringLiteral("Choose Folder…"), this, &SearchForm::chooseFolders);

    QStringList favorites;
    for (const HistoryEntry &entry : m_history->entries(HistoryField::Directories))
        if (entry.pinned) favorites.append(entry.value);
    if (!favorites.isEmpty()) {
        menu.addSection(QStringLiteral("Favorites"));
        for (const QString &favorite : favorites)
            menu.addAction(favorite, this, [this, favorite] { m_directories->setEditText(favorite); });
    }
    menu.addSeparator();

    const QString current = m_directories->currentText().trimmed();
    if (!current.isEmpty()) {
        const bool pinned = m_history->isPinned(current, HistoryField::Directories);
        menu.addAction(pinned ? QStringLiteral("Remove from Favorites") : QStringLiteral("Add to Favorites"), this, [this, current, pinned] {
            m_history->setPinned(!pinned, current, HistoryField::Directories);
            Q_EMIT historyChanged();
        });
        menu.addAction(QStringLiteral("Remove from History"), this, [this, current] {
            m_history->remove(current, HistoryField::Directories);
            Q_EMIT historyChanged();
        });
    }
    menu.addAction(QStringLiteral("Clear Recent Folders"), this, [this] {
        m_history->clear(HistoryField::Directories);
        Q_EMIT historyChanged();
    });
    auto *button = qobject_cast<QWidget *>(sender());
    menu.exec(button ? button->mapToGlobal(QPoint(0, button->height())) : QCursor::pos());
}

void SearchForm::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void SearchForm::dropEvent(QDropEvent *event)
{
    QStringList folders;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile()) continue;
        const QFileInfo info(url.toLocalFile());
        const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        if (!folders.contains(folder)) folders.append(folder);
    }
    if (folders.isEmpty()) return;
    m_directories->setEditText(folders.join(QLatin1Char(';')));
    event->acceptProposedAction();
}
