#pragma once

#include "Results.h"

#include <QPlainTextEdit>

/// Read-only file preview with line numbers and highlighted matches.
class PreviewView : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit PreviewView(QWidget *parent = nullptr);

    /// Shows text for `file`; the text is kept until a different path is shown.
    void showText(const QString &path, const QString &text, const garimpo::FileResult &file);
    void showMessage(const QString &message);
    void selectMatch(const garimpo::LineMatch *match);
    QString shownPath() const { return m_path; }
    void setFontSize(int size);

    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QTextCursor rangeCursor(int lineNumber, const garimpo::LineMatch &match, const garimpo::Span &span) const;
    void updateLineNumberArea(const QRect &rect, int dy);

    QWidget *m_lineNumbers;
    QString m_path;
    QList<QTextEdit::ExtraSelection> m_allMatches;
    std::vector<garimpo::LineMatch> m_matches;
};
