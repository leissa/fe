#pragma once

#include <sstream>
#include <string_view>

#include <fe/format.h>
#include <fe/loc.h>
#include <fe/sym.h>

namespace fe {

/// Use/derive from this class for "global" variables that you need all over the place.
/// Well, there are not really global - that's the point of this class.
/// Right now, it manages a SymPool (by inherting from it) and offers `std::format`-based diagnostics.
struct Driver : public SymPool {
public:
    /// @name Diagnostics
    ///@{
    // clang-format off
    template<class... Args> static void note(Loc loc, std::format_string<Args...> fmt, Args&&... args) {                  std::cerr << loc << ": note: "    << std::format(fmt, std::forward<Args>(args)...) << std::endl; }
    template<class... Args>        void warn(Loc loc, std::format_string<Args...> fmt, Args&&... args) { ++num_warnings_; std::cerr << loc << ": warning: " << std::format(fmt, std::forward<Args>(args)...) << std::endl; }
    template<class... Args>        void err (Loc loc, std::format_string<Args...> fmt, Args&&... args) { ++num_errors_;   std::cerr << loc << ": error: "   << std::format(fmt, std::forward<Args>(args)...) << std::endl; }
    // clang-format on

    unsigned num_errors() const { return num_errors_; }
    unsigned num_warnings() const { return num_warnings_; }
    ///@}

private:
    unsigned num_errors_   = 0;
    unsigned num_warnings_ = 0;
};

} // namespace fe
