#include "ReplaceDialog.h"

#include "AppSettings.h"
#include "SearchEngine.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace garimpo;

namespace {

constexpr int kFileRole = Qt::UserRole + 1;
constexpr int kLineRole = Qt::UserRole + 2;

} // namespace

ReplaceDialog::ReplaceDialog(const SearchQuery &query, const QString &replacement, QWidget *parent)
    : QDialog(parent), m_query(query), m_replacement(replacement), m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    setWindowTitle(QStringLiteral("Replace"));
    resize(960, 640);

    auto *summary = new QLabel(QStringLiteral("<b>%1</b> &rarr; <b>%2</b><br><small>%3</small>")
                                   .arg(query.pattern.toHtmlEscaped(),
                                        replacement.isEmpty() ? QStringLiteral("(empty)") : replacement.toHtmlEscaped(),
                                        query.directories.toHtmlEscaped()));
    summary->setTextFormat(Qt::RichText);

    m_stack = new QStackedWidget;
    auto *waiting = new QWidget;
    auto *waitingLayout = new QVBoxLayout(waiting);
    m_status = new QLabel(QStringLiteral("Finding lines to change…"));
    auto *progress = new QProgressBar;
    progress->setRange(0, 0);
    waitingLayout->addStretch();
    waitingLayout->addWidget(m_status, 0, Qt::AlignCenter);
    waitingLayout->addWidget(progress);
    waitingLayout->addStretch();

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({QStringLiteral("File / line"), QStringLiteral("Before"), QStringLiteral("After")});
    m_tree->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_tree->setColumnWidth(0, 260);
    m_tree->setColumnWidth(1, 330);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (m_updating || column != 0) return;
        m_updating = true;
        const Qt::CheckState state = item->checkState(0);
        if (item->childCount() > 0) {
            for (int i = 0; i < item->childCount(); ++i) item->child(i)->setCheckState(0, state);
        }
        m_updating = false;
        updateButton();
    });
    m_stack->addWidget(waiting);
    m_stack->addWidget(m_tree);

    m_skipped = new QLabel;
    m_skipped->setForegroundRole(QPalette::PlaceholderText);

    auto *buttons = new QDialogButtonBox;
    auto *all = buttons->addButton(QStringLiteral("Select All"), QDialogButtonBox::ResetRole);
    auto *none = buttons->addButton(QStringLiteral("Select None"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    m_replace = buttons->addButton(QStringLiteral("Replace"), QDialogButtonBox::AcceptRole);
    m_replace->setEnabled(false);
    m_replace->setDefault(true);
    connect(all, &QPushButton::clicked, this, [this] { setAll(true); });
    connect(none, &QPushButton::clicked, this, [this] { setAll(false); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *note = new QLabel(QStringLiteral("Every changed file is backed up first; Search ▸ Undo Last Replace restores it."));
    note->setForegroundRole(QPalette::PlaceholderText);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(summary);
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_skipped);
    layout->addWidget(note);
    layout->addWidget(buttons);

    // Find matching files and plan the changes off the UI thread.
    const SearchLimits limits = AppSettings::limits();
    auto cancel = m_cancel;
    QPointer<ReplaceDialog> self(this);
    QThread *thread = QThread::create([self, query, replacement, limits, cancel] {
        auto plan = std::make_shared<ReplacePlan>();
        QString error;
        try {
            QStringList paths;
            SearchEngine::run(query, limits, cancel.get(), nullptr, [&](std::vector<FileResult> &&files, const SearchStats &) {
                for (const FileResult &file : files)
                    if (file.kind == FileKind::Text) paths.append(file.path);
            });
            paths.sort();
            if (!cancel->load()) *plan = Replacer::plan(query, replacement, paths);
        } catch (const PatternError &problem) {
            error = problem.message();
        }
        QMetaObject::invokeMethod(self.data(), [self, plan, error] {
            if (!self) return;
            if (!error.isEmpty()) {
                self->m_status->setText(error);
                return;
            }
            self->m_plan = std::make_unique<ReplacePlan>(std::move(*plan));
            self->showPlan();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

ReplaceDialog::~ReplaceDialog()
{
    m_cancel->store(true);
}

void ReplaceDialog::showPlan()
{
    if (m_plan->files.empty()) {
        m_status->setText(QStringLiteral("No line would change."));
        return;
    }
    m_updating = true;
    for (size_t f = 0; f < m_plan->files.size(); ++f) {
        const FileChange &file = m_plan->files[f];
        auto *fileItem = new QTreeWidgetItem(m_tree);
        fileItem->setText(0, QFileInfo(file.path).fileName());
        fileItem->setText(1, QFileInfo(file.path).absolutePath());
        fileItem->setText(2, QStringLiteral("%1 lines · %2").arg(file.lines.size()).arg(file.encoding));
        fileItem->setToolTip(0, file.path);
        fileItem->setCheckState(0, Qt::Checked);
        fileItem->setData(0, kFileRole, int(f));
        QFont bold = fileItem->font(0);
        bold.setBold(true);
        fileItem->setFont(0, bold);
        for (size_t l = 0; l < file.lines.size(); ++l) {
            const LineChange &line = file.lines[l];
            auto *lineItem = new QTreeWidgetItem(fileItem);
            lineItem->setText(0, QStringLiteral("Line %1").arg(line.lineNumber));
            lineItem->setText(1, line.original.trimmed());
            lineItem->setText(2, line.replaced.trimmed());
            lineItem->setToolTip(1, line.original);
            lineItem->setToolTip(2, line.replaced);
            lineItem->setForeground(1, QColor(200, 60, 60));
            lineItem->setForeground(2, QColor(40, 150, 70));
            lineItem->setCheckState(0, Qt::Checked);
            lineItem->setData(0, kFileRole, int(f));
            lineItem->setData(0, kLineRole, int(l));
        }
    }
    m_tree->expandAll();
    m_updating = false;
    if (!m_plan->skipped.empty())
        m_skipped->setText(QStringLiteral("%1 files skipped (binary, unreadable or documents).").arg(m_plan->skipped.size()));
    m_stack->setCurrentWidget(m_tree);
    updateButton();
}

ReplacePlan ReplaceDialog::selectedPlan() const
{
    ReplacePlan plan = m_plan ? *m_plan : ReplacePlan{};
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *fileItem = m_tree->topLevelItem(i);
        FileChange &file = plan.files[size_t(fileItem->data(0, kFileRole).toInt())];
        file.included = fileItem->checkState(0) != Qt::Unchecked;
        for (int j = 0; j < fileItem->childCount(); ++j) {
            QTreeWidgetItem *lineItem = fileItem->child(j);
            file.lines[size_t(lineItem->data(0, kLineRole).toInt())].included = lineItem->checkState(0) == Qt::Checked;
        }
    }
    return plan;
}

void ReplaceDialog::updateButton()
{
    if (!m_plan) return;
    const ReplacePlan plan = selectedPlan();
    m_replace->setText(QStringLiteral("Replace %1 lines in %2 files").arg(plan.selectedLineCount()).arg(plan.selectedFileCount()));
    m_replace->setEnabled(plan.selectedLineCount() > 0);
}

void ReplaceDialog::setAll(bool checked)
{
    m_updating = true;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *fileItem = m_tree->topLevelItem(i);
        fileItem->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        for (int j = 0; j < fileItem->childCount(); ++j) fileItem->child(j)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    }
    m_updating = false;
    updateButton();
}
