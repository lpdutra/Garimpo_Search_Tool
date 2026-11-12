#include "TextLines.h"

#include <algorithm>

namespace garimpo {

std::vector<LineRef> TextLines::split(QStringView text)
{
    std::vector<LineRef> lines;
    const char16_t *data = text.utf16();
    const qsizetype size = text.size();
    qsizetype start = 0;
    for (qsizetype i = 0; i < size; ++i) {
        const char16_t ch = data[i];
        if (ch != u'\n' && ch != u'\r') continue;
        quint8 terminator = 1;
        if (ch == u'\r' && i + 1 < size && data[i + 1] == u'\n') terminator = 2;
        lines.push_back(LineRef{start, i - start, terminator});
        i += terminator - 1;
        start = i + 1;
    }
    if (start < size || lines.empty()) lines.push_back(LineRef{start, size - start, 0});
    return lines;
}

qsizetype TextLines::lineAt(const std::vector<LineRef> &lines, qsizetype position)
{
    auto it = std::upper_bound(lines.begin(), lines.end(), position,
                               [](qsizetype value, const LineRef &line) { return value < line.start; });
    return it == lines.begin() ? 0 : qsizetype(std::distance(lines.begin(), it) - 1);
}

} // namespace garimpo
