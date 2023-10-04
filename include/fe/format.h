#pragma once

#include <format>

#include "fe/loc.h"

namespace fe {

/// Make types that support ostream operators available for `std::format`.
/// Use like this:
/// ```
/// template<> struct std::formatter<T> : fe::ostream_formatter {};
/// ```
/// @sa [Stack Overflow](https://stackoverflow.com/a/75738462).
template<class Char> struct basic_ostream_formatter : std::formatter<std::basic_string_view<Char>, Char> {
    template<class T, class O> O format(const T& value, std::basic_format_context<O, Char>& ctx) const {
        std::basic_stringstream<Char> ss;
        ss << value;
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 170000
        return std::formatter<std::basic_string_view<Char>, Char>::format(ss.str(), ctx);
#else
        return std::formatter<std::basic_string_view<Char>, Char>::format(ss.view(), ctx);
#endif
    }
};

using ostream_formatter = basic_ostream_formatter<char>;

// clang-format off
/// @name out/outln/err/errln
///@{
/// Print to `std::cout`/`std::cerr` via `std::format`; the `*ln` variants conclude with `std::endl`.
template<class... Args> void err  (std::format_string<Args...> fmt, Args&&... args) { std::cerr << std::format(fmt, std::forward<Args&&>(args)...);              }
template<class... Args> void out  (std::format_string<Args...> fmt, Args&&... args) { std::cout << std::format(fmt, std::forward<Args&&>(args)...);              }
template<class... Args> void errln(std::format_string<Args...> fmt, Args&&... args) { std::cerr << std::format(fmt, std::forward<Args&&>(args)...) << std::endl; }
template<class... Args> void outln(std::format_string<Args...> fmt, Args&&... args) { std::cout << std::format(fmt, std::forward<Args&&>(args)...) << std::endl; }
// clang-format on

} // namespace fe

template<> struct std::formatter<fe::Pos> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Loc> : fe::ostream_formatter {};
