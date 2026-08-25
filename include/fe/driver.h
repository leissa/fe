#pragma once

#include <sstream>
#include <string_view>

#include <fe/format.h>
#include <fe/loc.h>
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
    ///@{
    // clang-format off
    template<class... Args> void note(Loc loc, std::format_string<Args...> fmt, Args&&... args) const {                  std::cerr << loc << ": " << term::FG::Cyan    << "note: "    << term::FG::Reset << std::format(fmt, std::forward<Args>(args)...) << std::endl; }
    template<class... Args> void warn(Loc loc, std::format_string<Args...> fmt, Args&&... args)       { ++num_warnings_; std::cerr << loc << ": " << term::FG::Magenta << "warning: " << term::FG::Reset << std::format(fmt, std::forward<Args>(args)...) << std::endl; }
    template<class... Args> void err (Loc loc, std::format_string<Args...> fmt, Args&&... args)       { ++num_errors_;   std::cerr << loc << ": " << term::FG::Red     << "error: "   << term::FG::Reset << std::format(fmt, std::forward<Args>(args)...) << std::endl; }
    // clang-format on

    unsigned num_errors() const { return num_errors_; }
    unsigned num_warnings() const { return num_warnings_; }
    ///@}

private:
    SrcMap src_;
    unsigned num_errors_   = 0;
    unsigned num_warnings_ = 0;
};

} // namespace fe
