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
template<class Char>
struct basic_ostream_formatter : std::formatter<std::basic_string_view<Char>, Char> {
    template<class T, class O>
    O format(const T& value, std::basic_format_context<O, Char>& ctx) const {
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

/// @name out/outln/err/errln
/// Print to `std::cout`/`std::cerr` via `std::format`; the `*ln` variants conclude with `std::endl`.
///@{
// clang-format off
template<class... Args> void err  (std::format_string<Args...> fmt, Args&&... args) { std::cerr << std::format(fmt, std::forward<Args>(args)...);              }
template<class... Args> void out  (std::format_string<Args...> fmt, Args&&... args) { std::cout << std::format(fmt, std::forward<Args>(args)...);              }
template<class... Args> void errln(std::format_string<Args...> fmt, Args&&... args) { std::cerr << std::format(fmt, std::forward<Args>(args)...) << std::endl; }
template<class... Args> void outln(std::format_string<Args...> fmt, Args&&... args) { std::cout << std::format(fmt, std::forward<Args>(args)...) << std::endl; }
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
    constexpr size_t indent() const noexcept { return indent_; }
    constexpr std::string_view tab() const noexcept { return tab_; }
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
    /// @name Creates a new Tab
    ///@{
    [[nodiscard]] constexpr Tab operator++(int) noexcept {                      return {tab_, indent_++}; }
    [[nodiscard]] constexpr Tab operator--(int) noexcept { assert(indent_ > 0); return {tab_, indent_--}; }
    [[nodiscard]] constexpr Tab operator+(size_t indent) const noexcept {                      return {tab_, indent_ + indent}; }
    [[nodiscard]] constexpr Tab operator-(size_t indent) const noexcept { assert(indent_ > 0); return {tab_, indent_ - indent}; }
    ///@}

    /// @name Modifies this Tab
    ///@{
    constexpr Tab& operator++() noexcept {                      ++indent_; return *this; }
    constexpr Tab& operator--() noexcept { assert(indent_ > 0); --indent_; return *this; }
    constexpr Tab& operator+=(size_t indent) noexcept {                      indent_ += indent; return *this; }
    constexpr Tab& operator-=(size_t indent) noexcept { assert(indent_ > 0); indent_ -= indent; return *this; }
    constexpr Tab& operator=(size_t indent) noexcept { indent_ = indent; return *this; }
    constexpr Tab& operator=(std::string_view tab) noexcept { tab_ = tab; return *this; }
    ///@}
    // clang-format on

    friend std::ostream& operator<<(std::ostream& os, Tab tab) {
        for (size_t i = 0; i != tab.indent_; ++i)
            os << tab.tab_;
        return os;
    }

private:
    std::string_view tab_;
    size_t indent_ = 0;
};

} // namespace fe

#ifndef DOXYGEN
// clang-format off
template<> struct std::formatter<fe::Pos> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Loc> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Sym> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Tab> : fe::ostream_formatter {};
template<> struct std::formatter<fe::utf8::Char32> : fe::ostream_formatter {};
// clang-format on
#endif
