#pragma once

#include "Replace.h"

#include <QDialog>

#include <memory>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

/// Preview of a replace: every changed line with a checkbox, built in the background.
class ReplaceDialog : public QDialog {
    Q_OBJECT

public:
    ReplaceDialog(const garimpo::SearchQuery &query, const QString &replacement, QWidget *parent = nullptr);
    ~ReplaceDialog() override;

    /// The plan with the user's selection applied.
    garimpo::ReplacePlan selectedPlan() const;

private:
    void showPlan();
    void updateButton();
    void setAll(bool checked);

    garimpo::SearchQuery m_query;
    QString m_replacement;
    std::unique_ptr<garimpo::ReplacePlan> m_plan;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    QStackedWidget *m_stack;
    QLabel *m_status;
    QTreeWidget *m_tree;
    QPushButton *m_replace;
    QLabel *m_skipped;
    bool m_updating = false;
};
