#pragma once

#include <format>

#include "fe/loc.h"
#include "fe/utf8.h"

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

/// Wrapper for `char32_t` which has a friend ostream operator.
struct Char32 {
    Char32(char32_t c)
        : c(c) {}

    friend std::ostream& operator<<(std::ostream& os, Char32 c) {
        auto res = utf8::encode(os, c.c);
        assert_unused(res);
        return os;
    }

    char32_t c;
};

// clang-format off
/// @name out/outln/err/errln
///@{
/// Print to `std::cout`/`std::cerr` via `std::format`; the `*ln` variants conclude with `std::endl`.
template<class... Args> void err  (std::format_string<Args...> fmt, Args&&... args) { std::cerr << std::format(fmt, std::forward<Args&&>(args)...);              }
template<class... Args> void out  (std::format_string<Args...> fmt, Args&&... args) { std::cout << std::format(fmt, std::forward<Args&&>(args)...);              }
template<class... Args> void errln(std::format_string<Args...> fmt, Args&&... args) { std::cerr << std::format(fmt, std::forward<Args&&>(args)...) << std::endl; }
template<class... Args> void outln(std::format_string<Args...> fmt, Args&&... args) { std::cout << std::format(fmt, std::forward<Args&&>(args)...) << std::endl; }
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
template<> struct std::formatter<fe::Pos> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Loc> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Sym> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Tab> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Char32> : fe::ostream_formatter {};
#endif
