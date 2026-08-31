#include "fe/diag.h"

#include <format>
#include <ostream>
#include <sstream>

#include "fe/assert.h"
#include "fe/snippet.h"
#include "fe/src.h"

namespace fe {

namespace {

/// Index of the next backtick at or after @p i that is not escaped as `` \` ``, or `npos`.
size_t tick(std::string_view str, size_t i) {
    for (; i != str.size(); ++i)
        if (str[i] == '\\' && i + 1 != str.size() && str[i + 1] == '`')
            ++i;
        else if (str[i] == '`')
            return i;
    return std::string_view::npos;
}

/// Streams `[begin, end)` of @p str, dropping the backslash of every `` \` ``.
void stream_raw(std::ostream& os, std::string_view str, size_t begin, size_t end) {
    for (auto i = begin; i != end; ++i) {
        if (str[i] == '\\' && i + 1 != end && str[i + 1] == '`') ++i;
        os << str[i];
    }
}

} // namespace

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
    auto str   = fmt();
    auto oss   = std::ostringstream();
    auto color = term::use_color(oss);

    for (size_t i = 0, e = str.size(); i != e;) {
        auto l = tick(str, i);
        auto r = l == std::string_view::npos ? l : tick(str, l + 1);
        if (r == std::string_view::npos) { // unpaired: not a citation
            stream_raw(oss, str, i, e);
            break;
        }

        stream_raw(oss, str, i, l);
        if (color)
            oss << term::FG::Cyan;
        else
            oss << '`';
        stream_raw(oss, str, l + 1, r);
        if (color)
            oss << term::FG::Reset;
        else
            oss << '`';
        i = r + 1;
    }

    return oss.str();
}

} // namespace fe
