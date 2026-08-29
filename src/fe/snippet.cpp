#include "fe/snippet.h"

#include <algorithm>

#include "fe/format.h"
#include "fe/src.h"
#include "fe/utf8.h"

namespace fe {

namespace {

/// Streams @p row of the source and a caret run underlining the code points in `[begin, end)`.
void stream_row(std::ostream& os,
                std::string_view line,
                uint32_t row,
                size_t begin,
                size_t end,
                term::FG color,
                uint32_t gutter) {
    os << term::FG::Gray << std::format("{:>{}} | ", row, gutter) << term::FG::Reset << line << '\n';
    if (end <= begin) return;

    os << term::FG::Gray << std::format("{:>{}} | ", "", gutter) << color;

    // One blank per *code point*, since that is what a column counts;
    // a tab is echoed rather than blanked, or the carets drift by its width.
    for (size_t i = 0, c = 0; c != begin && i < line.size(); ++c) {
        bool tab = line[i] == '\t';
        utf8::decode(line, i);
        os << (tab ? '\t' : ' ');
    }
    for (size_t i = begin; i != end; ++i)
        os << '^';

    os << term::FG::Reset << '\n';
}

} // namespace

std::ostream& operator<<(std::ostream& os, const Snippet& snippet) {
    auto [loc, color, gutter, max_rows] = snippet;

    auto src = loc.src;
    if (!src) return os;

    auto [first_row, first_col] = src->rowcol(loc.begin);
    auto [last_row, last_col]   = src->rowcol(src->prev(loc.end));
    if (first_row == 0) return os;
    if (last_row < first_row) last_row = first_row, last_col = first_col;

    if (first_row == last_row && size_t(first_col) - 1 >= utf8::num_code_points(src->line(first_row))) return os;

    for (auto row = first_row; row <= last_row; ++row) {
        if (max_rows != 0 && last_row - first_row + 1 > max_rows && row == first_row + max_rows / 2) {
            os << term::FG::Gray << std::format("{:>{}} |\n", "...", gutter) << term::FG::Reset;
            row = last_row - (max_rows - max_rows / 2 - 1);
        }

        auto line  = src->line(row);
        auto len   = utf8::num_code_points(line);
        auto begin = std::min(row == first_row ? size_t(first_col) - 1 : 0, len);
        auto end   = std::min(std::max(row == last_row ? size_t(last_col) : len, begin + 1), len);
        stream_row(os, line, row, begin, end, color, gutter);
    }

    return os;
}

} // namespace fe
