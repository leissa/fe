#pragma once

#include <concepts>
#include <format>
#include <functional>
#include <ostream>
#include <ranges>
#include <utility>

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
        return std::formatter<std::basic_string_view<Char>, Char>::format(ss.view(), ctx);
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

    // clang-format off
    /// @name Creates a new Tab
    ///@{
    [[nodiscard]] Tab operator++(int) noexcept {                      return {tab_, indent_++}; }
    [[nodiscard]] Tab operator--(int) noexcept { assert(indent_ > 0); return {tab_, indent_--}; }
    [[nodiscard]] Tab operator+(size_t indent) const noexcept {                      return {tab_, indent_ + indent}; }
    [[nodiscard]] Tab operator-(size_t indent) const noexcept { assert(indent_ > 0); return {tab_, indent_ - indent}; }
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

    /// @name Formatted Output
    /// Wrap `std::format` to prefix the formatted string with the current indentation.
    ///@{
    // clang-format off
    template<class... Args> std::ostream& print  (std::ostream& os, std::format_string<Args...> fmt, Args&&... args) const { return  os << *this  << std::format(fmt, std::forward<Args>(args)...);                }
    template<class... Args> std::ostream& println(std::ostream& os, std::format_string<Args...> fmt, Args&&... args) const { return (os << *this  << std::format(fmt, std::forward<Args>(args)...)) << std::endl;  }
    template<class... Args> std::ostream& lnprint(std::ostream& os, std::format_string<Args...> fmt, Args&&... args) const { return  os << std::endl << *this << std::format(fmt, std::forward<Args>(args)...);     }
    // clang-format on
    ///@}

private:
    std::string_view tab_;
    size_t indent_ = 0;
};

/// Wrap a callable `f(std::ostream&) -> std::ostream&` so it streams via `operator<<` and `std::format`.
/// Useful for inline ad-hoc formatting:
/// ```
/// auto greet = fe::StreamFn{[](std::ostream& os) -> std::ostream& { return os << "hi"; }};
/// std::cout  << greet;
/// std::format("{}", greet);
/// ```
template<class F>
    requires std::invocable<const F&, std::ostream&>
class StreamFn {
public:
    constexpr StreamFn(F f)
        : f_(std::move(f)) {}

    friend std::ostream& operator<<(std::ostream& os, const StreamFn& s) {
        if constexpr (std::is_void_v<std::invoke_result_t<const F&, std::ostream&>>)
            return std::invoke(s.f_, os), os;
        else
            return std::invoke(s.f_, os);
    }

private:
    F f_;
};

template<class F> StreamFn(F) -> StreamFn<F>;

/// Join elements of @p range with @p sep.
/// Use as a `std::format` argument: `std::format("{}", fe::join(v, ", "))`.
/// The @p range must outlive the returned object.
template<std::ranges::input_range R>
class Join {
public:
    Join(const R& range, std::string_view sep)
        : range_(range)
        , sep_(sep) {}

    const R& range() const { return range_; }
    std::string_view sep() const { return sep_; }

private:
    const R& range_;
    std::string_view sep_;
};

template<class R> Join(const R&, std::string_view) -> Join<R>;

template<std::ranges::input_range R>
Join<R> join(const R& range, std::string_view sep) {
    return Join<R>(range, sep);
}

} // namespace fe

#ifndef DOXYGEN
template<class F>
struct std::formatter<fe::StreamFn<F>> : fe::ostream_formatter {};

template<class R>
struct std::formatter<fe::Join<R>> {
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    template<class FormatContext>
    auto format(const fe::Join<R>& j, FormatContext& ctx) const {
        auto out   = ctx.out();
        bool first = true;
        for (const auto& elem : j.range()) {
            if (!first) out = std::format_to(out, "{}", j.sep());
            out   = std::format_to(out, "{}", elem);
            first = false;
        }
        return out;
    }
};

// clang-format off
template<> struct std::formatter<fe::Pos> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Loc> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Sym> : fe::ostream_formatter {};
template<> struct std::formatter<fe::Tab> : fe::ostream_formatter {};
template<> struct std::formatter<fe::utf8::Char32> : fe::ostream_formatter {};
// clang-format on
#endif
