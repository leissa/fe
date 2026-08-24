#pragma once

#include <cstddef>

#include <filesystem>
#include <format>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef FE_ABSL
#    include <absl/container/flat_hash_map.h>
#else
#    include <unordered_map>
#endif

#include "fe/loc.h"
#include "fe/term.h"
#include "fe/utf8.h"

/// Renders the source a Loc points at underneath a diagnostic, the way clang and rustc do:
/// ```
///     3 | let n = id 3;
///       |         ^^^^
/// ```
/// @ref fe::snippet draws one, @ref fe::Src supplies the lines to draw.

namespace fe {

/// Streams @p line, then a caret run underlining the columns @p loc spans.
/// Both sit behind a gutter of width @p gutter holding the row number.
/// Does nothing if @p line is empty or @p loc has no column, since then there is nothing to point at.
/// A @p loc spanning several rows underlines the remainder of its first one.
inline std::ostream&
snippet(std::ostream& os, Loc loc, std::string_view line, term::FG caret = term::FG::Red, int gutter = 5) {
    // A col of 0 is Pos's "unknown" sentinel.
    if (line.empty() || loc.begin.col == 0) return os;

    auto len   = utf8::num_code_points(line);
    auto begin = size_t(loc.begin.col) - 1;
    if (begin >= len) return os;

    auto finis = loc.finis.row != loc.begin.row || loc.finis.col == 0 ? len : size_t(loc.finis.col);
    finis      = std::min(std::max(finis, begin + 1), len);

    os << term::FG::Gray << std::format("{:>{}} | ", loc.begin.row, gutter) << term::FG::Reset << line << '\n'
       << term::FG::Gray << std::format("{:>{}} | ", "", gutter) << caret;

    // One blank per *code point*, since that is what Pos::col counts;
    // a tab is echoed rather than blanked, or the carets drift by its width.
    for (size_t i = 0, c = 0; c != begin && i < line.size(); ++c) {
        auto n = utf8::num_bytes(char8_t(line[i]));
        os << (line[i] == '\t' ? '\t' : ' ');
        i += n == 0 ? 1 : n;
    }
    for (size_t i = begin; i != finis; ++i)
        os << '^';

    return os << term::FG::Reset << '\n';
}

/// The source lines a diagnostic refers to, read on demand and memoized per file.
/// @warning Loc::path is borrowed, so a Src must not outlive the paths handed to it.
/// Keep one per rendering rather than one for the process.
class Src {
public:
    /// The line Loc::begin points at, or empty if the file or that row is unavailable.
    std::string_view line(Loc loc) {
        if (!loc || !loc.path) return {};

        auto [i, ins] = path2lines_.try_emplace(loc.path);
        if (ins) {
            auto ifs = std::ifstream(*loc.path);
            for (std::string line; std::getline(ifs, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                i->second.emplace_back(std::move(line));
            }
        }

        auto row = loc.begin.row;
        return row <= i->second.size() ? std::string_view(i->second[row - 1]) : std::string_view();
    }

private:
#ifdef FE_ABSL
    absl::flat_hash_map<const std::filesystem::path*, std::vector<std::string>> path2lines_;
#else
    std::unordered_map<const std::filesystem::path*, std::vector<std::string>> path2lines_;
#endif
};

} // namespace fe
