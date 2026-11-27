#pragma once

#include "Results.h"
#include "Syntax.h"

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

    /// Colors the text shown by `showText`. `lineStates` comes from Syntax::lineStates over the same text;
    /// lines are colored as they scroll into view, so the cost does not grow with the file.
    void setSyntax(const garimpo::SyntaxLanguage *language, std::vector<int> lineStates);
    bool hasSyntax() const { return m_language != nullptr; }

    int lineNumberAreaWidth() const;
    void paintLineNumbers(QPaintEvent *event);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QTextCursor rangeCursor(int lineNumber, const garimpo::LineMatch &match, const garimpo::Span &span) const;
    void updateLineNumberArea(const QRect &rect, int dy);
    void clearSyntax();
    void highlightVisibleLines();

    QWidget *m_lineNumbers;
    QString m_path;
    QList<QTextEdit::ExtraSelection> m_allMatches;
    std::vector<garimpo::LineMatch> m_matches;

    const garimpo::SyntaxLanguage *m_language = nullptr;
    std::vector<int> m_lineStates;
    std::vector<bool> m_colored;
    QTextCharFormat m_syntaxFormats[garimpo::kSyntaxTokenCount];
};
