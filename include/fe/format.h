#pragma once

#ifdef FE_STD_FORMAT_SUPPORT
#    include <format>
#else
#    include <fmt/format.h>
#endif

#include "fe/loc.h"
#include "fe/utf8.h"

namespace fe {

namespace format {
#ifdef FE_STD_FORMAT_SUPPORT
using namespace ::std;
#else
using namespace ::fmt;
#endif
} // namespace format

/// Make types that support ostream operators available for `std::format`.
/// Use like this:
/// ```
/// template<> struct std::formatter<T> : fe::ostream_formatter {};
/// ```
/// @sa [Stack Overflow](https://stackoverflow.com/a/75738462).
template<class Char> struct basic_ostream_formatter : format::formatter<std::basic_string_view<Char>, Char> {
    template<class T, class O> O format(const T& value, format::basic_format_context<O, Char>& ctx) const {
        std::basic_stringstream<Char> ss;
        ss << value;
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 170000
        return std::formatter<std::basic_string_view<Char>, Char>::format(ss.str(), ctx);
#else
        return format::formatter<std::basic_string_view<Char>, Char>::format(ss.view(), ctx);
#endif
    }
};

using ostream_formatter = basic_ostream_formatter<char>;

/// @name out/outln/err/errln
/// Print to `std::cout`/`std::cerr` via `std::format`; the `*ln` variants conclude with `std::endl`.
///@{
// clang-format off
template<class... Args> void err  (format::format_string<Args...> fmt, Args&&... args) { std::cerr << format::format(fmt, std::forward<Args&&>(args)...);              }
template<class... Args> void out  (format::format_string<Args...> fmt, Args&&... args) { std::cout << format::format(fmt, std::forward<Args&&>(args)...);              }
template<class... Args> void errln(format::format_string<Args...> fmt, Args&&... args) { std::cerr << format::format(fmt, std::forward<Args&&>(args)...) << std::endl; }
template<class... Args> void outln(format::format_string<Args...> fmt, Args&&... args) { std::cout << format::format(fmt, std::forward<Args&&>(args)...) << std::endl; }
// clang-format on

/// Keeps track of indentation level during output
class Tab {
public:
    Tab(const Tab&) = default;
    Tab(std::string_view tab = {"\t"}, size_t indent = 0)
        : tab_(tab)
        , indent_(indent) {}

    /// @name Getters
    ///@{
    size_t indent() const { return indent_; }
    std::string_view tab() const { return tab_; }
    ///@}

    /// @name Setters
    ///@{
    Tab& operator=(size_t indent) {
        indent_ = indent;
        return *this;
    }
    Tab& operator=(std::string tab) {
        tab_ = tab;
        return *this;
    }
    ///@}

    // clang-format off
    /// @name Indent/Dedent
    ///@{
    Tab& operator++() {                      ++indent_; return *this; }
    Tab& operator--() { assert(indent_ > 0); --indent_; return *this; }
    Tab& operator+=(size_t indent) {                      indent_ += indent; return *this; }
    Tab& operator-=(size_t indent) { assert(indent_ > 0); indent_ -= indent; return *this; }
    Tab  operator++(int) {                      auto res = *this; ++indent_; return res; }
    Tab  operator--(int) { assert(indent_ > 0); auto res = *this; --indent_; return res; }
    Tab  operator+(size_t indent) const {                      return {tab_, indent_ + indent}; }
    Tab  operator-(size_t indent) const { assert(indent_ > 0); return {tab_, indent_ - indent}; }
    ///@}
    // clang-format on

    friend std::ostream& operator<<(std::ostream& os, Tab tab) {
        for (size_t i = 0; i != tab.indent_; ++i) os << tab.tab_;
        return os;
    }

private:
    std::string_view tab_;
    size_t indent_ = 0;
};

} // namespace fe

#ifndef DOXYGEN
template<> struct fe::format::formatter<fe::Pos> : fe::ostream_formatter {};
template<> struct fe::format::formatter<fe::Loc> : fe::ostream_formatter {};
template<> struct fe::format::formatter<fe::Sym> : fe::ostream_formatter {};
template<> struct fe::format::formatter<fe::Tab> : fe::ostream_formatter {};
template<> struct fe::format::formatter<fe::utf8::Char32> : fe::ostream_formatter {};
#endif
