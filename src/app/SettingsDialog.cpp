#include "SettingsDialog.h"

#include "AppSettings.h"
#include "Documents.h"
#include "Replace.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

using namespace garimpo;

namespace {

QLabel *note(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setForegroundRole(QPalette::PlaceholderText);
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(History *history, QWidget *parent) : QDialog(parent), m_history(history)
{
    setWindowTitle(QStringLiteral("Settings"));
    resize(640, 480);
    auto *tabs = new QTabWidget;
    tabs->addTab(generalPage(), QStringLiteral("General"));
    tabs->addTab(historyPage(), QStringLiteral("History"));
    tabs->addTab(storagePage(), QStringLiteral("Storage"));
    auto *close = new QPushButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(close, 0, Qt::AlignRight);
}

QWidget *SettingsDialog::generalPage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);

    auto *editor = new QLineEdit(AppSettings::editorCommand());
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor->setPlaceholderText(QStringLiteral("Empty opens files with their default app"));
    connect(editor, &QLineEdit::textChanged, this, [](const QString &text) { AppSettings::setEditorCommand(text); });
    form->addRow(QStringLiteral("Editor command"), editor);
    form->addRow(QString(), note(QStringLiteral("Runs in your login shell; {file}, {line} and {column} are replaced. "
                                                "Examples: code -g {file}:{line}:{column} · subl {file}:{line} · gedit +{line} {file}")));

    auto *newTab = new QCheckBox(QStringLiteral("Open each search in a new tab"));
    newTab->setChecked(AppSettings::openInNewTab());
    connect(newTab, &QCheckBox::toggled, this, [](bool on) { AppSettings::setOpenInNewTab(on); });
    form->addRow(QString(), newTab);

    auto *restore = new QCheckBox(QStringLiteral("Reopen tabs from the last session"));
    restore->setChecked(AppSettings::restoreSession());
    connect(restore, &QCheckBox::toggled, this, [](bool on) { AppSettings::setRestoreSession(on); });
    form->addRow(QString(), restore);

    auto *historySize = new QSpinBox;
    historySize->setRange(5, 200);
    historySize->setSingleStep(5);
    historySize->setValue(AppSettings::historyLimit());
    historySize->setSuffix(QStringLiteral(" values per field"));
    connect(historySize, &QSpinBox::valueChanged, this, [this](int value) {
        AppSettings::setHistoryLimit(value);
        m_history->setLimit(value);
        Q_EMIT historyChanged();
    });
    form->addRow(QStringLiteral("Remember"), historySize);

    const SearchLimits limits = AppSettings::limits();
    auto *maxSize = new QSpinBox;
    maxSize->setRange(1, 4096);
    maxSize->setSuffix(QStringLiteral(" MB"));
    maxSize->setValue(int(limits.maxFileSizeMB));
    auto *maxLines = new QSpinBox;
    maxLines->setRange(10, 1000000);
    maxLines->setSingleStep(100);
    maxLines->setValue(limits.maxLinesPerFile);
    auto *maxTotal = new QSpinBox;
    maxTotal->setRange(100, 10000000);
    maxTotal->setSingleStep(10000);
    maxTotal->setValue(int(limits.maxTotalMatches));
    auto saveLimits = [maxSize, maxLines, maxTotal] {
        SearchLimits updated;
        updated.maxFileSizeMB = maxSize->value();
        updated.maxLinesPerFile = maxLines->value();
        updated.maxTotalMatches = maxTotal->value();
        AppSettings::setLimits(updated);
    };
    for (QSpinBox *box : {maxSize, maxLines, maxTotal}) connect(box, &QSpinBox::valueChanged, this, saveLimits);
    form->addRow(QStringLiteral("Skip text files over"), maxSize);
    form->addRow(QStringLiteral("Lines per file"), maxLines);
    form->addRow(QStringLiteral("Matches per search"), maxTotal);
    form->addRow(QString(), note(QStringLiteral("Limits keep memory low on huge folders; the status bar tells when one is reached.")));
    return page;
}

QWidget *SettingsDialog::historyPage()
{
    auto *page = new QWidget;
    m_field = new QComboBox;
    m_field->addItem(QStringLiteral("Folders"), int(HistoryField::Directories));
    m_field->addItem(QStringLiteral("Find"), int(HistoryField::Patterns));
    m_field->addItem(QStringLiteral("Replace"), int(HistoryField::Replacements));
    m_field->addItem(QStringLiteral("File masks"), int(HistoryField::IncludeMasks));
    m_field->addItem(QStringLiteral("Exclude masks"), int(HistoryField::ExcludeMasks));
    connect(m_field, &QComboBox::currentIndexChanged, this, &SettingsDialog::reloadHistory);

    m_entries = new QTreeWidget;
    m_entries->setHeaderLabels({QStringLiteral("Value"), QStringLiteral("Last used")});
    m_entries->setRootIsDecorated(false);
    m_entries->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_entries->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_entries->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto selected = [this] {
        QStringList values;
        for (QTreeWidgetItem *item : m_entries->selectedItems()) values.append(item->data(0, Qt::UserRole).toString());
        return values;
    };
    auto *pin = new QPushButton(QStringLiteral("Pin / Unpin"));
    connect(pin, &QPushButton::clicked, this, [this, selected] {
        for (const QString &value : selected()) m_history->setPinned(!m_history->isPinned(value, currentField()), value, currentField());
        reloadHistory();
        Q_EMIT historyChanged();
    });
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    connect(remove, &QPushButton::clicked, this, [this, selected] {
        for (const QString &value : selected()) m_history->remove(value, currentField());
        reloadHistory();
        Q_EMIT historyChanged();
    });
    auto *clear = new QPushButton(QStringLiteral("Clear Unpinned"));
    connect(clear, &QPushButton::clicked, this, [this] {
        m_history->clear(currentField());
        reloadHistory();
        Q_EMIT historyChanged();
    });

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(note(QStringLiteral("★ pinned values stay on top and survive clearing.")), 1);
    buttons->addWidget(pin);
    buttons->addWidget(remove);
    buttons->addWidget(clear);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(m_field);
    layout->addWidget(m_entries, 1);
    layout->addLayout(buttons);
    reloadHistory();
    return page;
}

HistoryField SettingsDialog::currentField() const
{
    return HistoryField(m_field->currentData().toInt());
}

void SettingsDialog::reloadHistory()
{
    m_entries->clear();
    for (const HistoryEntry &entry : m_history->entries(currentField())) {
        auto *item = new QTreeWidgetItem(m_entries);
        item->setText(0, (entry.pinned ? QStringLiteral("★ ") : QString()) + entry.value);
        item->setData(0, Qt::UserRole, entry.value);
        item->setText(1, QLocale().toString(entry.lastUsed, QLocale::ShortFormat));
    }
}

QWidget *SettingsDialog::storagePage()
{
    auto *page = new QWidget;
    auto *form = new QFormLayout(page);

    auto *cacheSize = new QLabel;
    auto *backups = new QLabel;
    auto refresh = [cacheSize, backups] {
        cacheSize->setText(QLocale().formattedDataSize(ExtractionCache::shared().sizeOnDisk()));
        const auto batches = Replacer::batches();
        backups->setText(QStringLiteral("%1 replaces, %2").arg(batches.size())
                             .arg(QLocale().formattedDataSize(AppPaths::folderSize(AppPaths::backupsDirectory()))));
    };
    refresh();

    auto *clean = new QPushButton(QStringLiteral("Remove Stale Entries"));
    connect(clean, &QPushButton::clicked, this, [this, refresh] {
        const auto report = ExtractionCache::shared().removeStale();
        refresh();
        QMessageBox::information(this, QStringLiteral("Cache"),
                                 QStringLiteral("Removed %1 stale entries, kept %2.").arg(report.removed).arg(report.kept));
    });
    auto *clearCache = new QPushButton(QStringLiteral("Clear Cache"));
    connect(clearCache, &QPushButton::clicked, this, [refresh] {
        ExtractionCache::shared().clear();
        refresh();
    });
    auto *cacheButtons = new QHBoxLayout;
    cacheButtons->addWidget(clean);
    cacheButtons->addWidget(clearCache);
    cacheButtons->addStretch();

    auto *openBackups = new QPushButton(QStringLiteral("Open Folder"));
    connect(openBackups, &QPushButton::clicked, this, [] {
        QDir().mkpath(AppPaths::backupsDirectory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::backupsDirectory()));
    });
    auto *deleteBackups = new QPushButton(QStringLiteral("Delete All Backups"));
    connect(deleteBackups, &QPushButton::clicked, this, [this, refresh] {
        if (QMessageBox::question(this, QStringLiteral("Delete backups"),
                                  QStringLiteral("Delete every replace backup? Undo will no longer be possible.")) != QMessageBox::Yes)
            return;
        Replacer::clearBackups();
        refresh();
    });
    auto *backupButtons = new QHBoxLayout;
    backupButtons->addWidget(openBackups);
    backupButtons->addWidget(deleteBackups);
    backupButtons->addStretch();

    form->addRow(QStringLiteral("Document text cache"), cacheSize);
    form->addRow(QString(), cacheButtons);
    form->addRow(QString(), note(QStringLiteral("Text extracted from PDF and office documents is cached so later searches are fast.")));
    form->addRow(QStringLiteral("Replace backups"), backups);
    form->addRow(QString(), backupButtons);
    const QString pdf = DocumentExtractor::pdfToTextPath();
    form->addRow(QStringLiteral("PDF support"),
                 new QLabel(pdf.isEmpty() ? QStringLiteral("pdftotext not found — install poppler to search PDF files") : pdf));
    form->addRow(QStringLiteral("Data folder"), new QLabel(AppPaths::base()));
    return page;
}
