#pragma once

#include <QString>
#include <QStringView>

#include <vector>

namespace garimpo {

/// Position of one line inside a text, without copying it.
struct LineRef {
    qsizetype start = 0;
    qsizetype length = 0;
    /// 0 for the last line, 1 for "\n" or "\r", 2 for "\r\n".
    quint8 terminatorLength = 0;
};

class TextLines {
public:
    /// Splits on "\n", "\r\n" and "\r". A trailing terminator does not add an empty line.
    static std::vector<LineRef> split(QStringView text);

    /// Index of the line containing `position`.
    static qsizetype lineAt(const std::vector<LineRef> &lines, qsizetype position);

    static QStringView content(QStringView text, const LineRef &line) { return text.sliced(line.start, line.length); }
};

} // namespace garimpo
