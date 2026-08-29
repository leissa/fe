#pragma once

#include <sstream>
#include <string_view>

#include <fe/format.h>
#include <fe/loc.h>
#include <fe/snippet.h>
#include <fe/src.h>
#include <fe/sym.h>
#include <fe/term.h>

namespace fe {

/// Use/derive from this class for "global" variables that you need all over the place.
/// Well, there are not really global - that's the point of this class.
/// Right now, it manages a SymPool and a SrcMap and offers `std::format`-based diagnostics.
struct Driver : public SymPool {
public:
    /// @name Source Files
    /// Register every file you lex here: a Loc is only as good as the SrcMap that keeps its Src alive.
    ///@{
    SrcMap& src() { return src_; }
    const SrcMap& src() const { return src_; }
    ///@}

    /// @name Diagnostics
    /// A diagnostic is the header line `loc: tag: msg` followed by the source snippet @p loc points at.
    ///@{
    // clang-format off
    template<class... Args> void note(Loc loc, std::format_string<Args...> fmt, Args&&... args) const {                  diag(loc, term::FG::Cyan,    "note",    std::format(fmt, std::forward<Args>(args)...)); }
    template<class... Args> void warn(Loc loc, std::format_string<Args...> fmt, Args&&... args)       { ++num_warnings_; diag(loc, term::FG::Magenta, "warning", std::format(fmt, std::forward<Args>(args)...)); }
    template<class... Args> void err (Loc loc, std::format_string<Args...> fmt, Args&&... args)       { ++num_errors_;   diag(loc, term::FG::Red,     "error",   std::format(fmt, std::forward<Args>(args)...)); }
    // clang-format on

    void diag(Loc loc, term::FG color, std::string_view tag, std::string_view msg) const {
        std::cerr << loc << ": " << color << tag << ": " << term::FG::Reset << msg << std::endl;
        if (!no_snippet) std::cerr << Snippet{loc, color, gutter, max_rows};
    }

    unsigned num_errors() const { return num_errors_; }
    unsigned num_warnings() const { return num_warnings_; }
    ///@}

    /// @name Diagnostic Layout
    ///@{
    uint32_t gutter   = 5;     ///< Width of the line-number column.
    uint32_t max_rows = 8;     ///< Rows a snippet streams before eliding its middle; `0` elides nothing.
    bool no_snippet   = false; ///< If `true`, only the header line is streamed.
    ///@}

private:
    SrcMap src_;
    unsigned num_errors_   = 0;
    unsigned num_warnings_ = 0;
};

} // namespace fe
