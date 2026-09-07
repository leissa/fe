#include "fe/diag.h"

#include <format>
#include <ostream>
#include <sstream>

#include "fe/assert.h"
#include "fe/snippet.h"
#include "fe/src.h"

namespace fe {

std::ostream& operator<<(std::ostream& os, Diag::Tag tag) {
    // clang-format off
    switch (tag) {
        case Diag::Tag::Error: return os << term::FG::Red     << "error";
        case Diag::Tag::Warn:  return os << term::FG::Magenta << "warning";
        case Diag::Tag::Note:  return os << term::FG::Green   << "note";
        default: unreachable();
    }
    // clang-format on
}

term::FG Diag::tag2color(Tag tag) {
    // clang-format off
    switch (tag) {
        case Tag::Error: return term::FG::Red;
        case Tag::Warn:  return term::FG::Magenta;
        case Tag::Note:  return term::FG::Green;
        default: unreachable();
    }
    // clang-format on
}

Diag::~Diag() = default;

void Diag::loc(std::ostream& os, Loc loc) const {
    auto src = loc.src;
    if (loc_style == Loc::Style::Full || !loc || !src || !src->contains(loc.begin)) {
        os << loc;
        return;
    }

    auto [row, col] = src->rowcol(loc.begin);
    if (row == 0) {
        os << loc;
        return;
    }

    auto path = src->path().string();
    // clang-format off
    switch (loc_style) {
        case Loc::Style::RowCol: os << path << ':' << row << ':' << col;         break;
        case Loc::Style::Row:    os << path << ':' << row;                       break;
        case Loc::Style::MSVC:   os << path << '(' << row << ',' << col << ')';  break;
        default: unreachable();
    }
    // clang-format on
}

/// Streamed piecewise instead of via std::format: a std::formatter cannot see its destination stream,
/// so embedded term::FG values would resolve Mode::Auto to "no color"; see fe/term.h.
void Diag::header(std::ostream& os, Loc l, Tag tag, std::string_view str) const {
    os << term::FG::Yellow;
    loc(os, l);
    os << ": " << tag << ": " << term::FG::Reset << str << '\n';
}

void Diag::snippet(std::ostream& os, Loc l, Tag tag) const {
    if (!no_snippet) os << Snippet{l, tag2color(tag), gutter, max_rows};
}

void Diag::note(std::ostream& os, Loc l, std::string_view str) const {
    if (l) {
        os << std::format("{:>{}} ", "", gutter);
        header(os, l, Tag::Note, str);
        snippet(os, l, Tag::Note);
    } else {
        os << term::FG::Gray << std::format("{:>{}} = ", "", gutter) << Tag::Note << ": " << term::FG::Reset << str
           << '\n';
    }
}

void Diag::summary(std::ostream& os, size_t num_errors, size_t num_warnings, bool truncated) const {
    if (num_errors == 0 && num_warnings == 0) return;

    auto sep = std::string_view();
    if (num_errors != 0) {
        os << sep << num_errors << " error(s)";
        sep = ", ";
    }
    if (num_warnings != 0) os << sep << num_warnings << " warning(s)";
    os << " encountered";
    if (truncated) os << "; further diagnostics dropped";
    os << '\n';
}

std::string CodeDiag::render(const std::function<std::string()>& fmt) const {
    auto str = fmt();
    auto oss = std::ostringstream();
    term::render_cite(oss, str);
    return oss.str();
}

} // namespace fe
