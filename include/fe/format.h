#pragma once

#include <concepts>

#include <format>
#include <functional>
#include <iostream>
#include <ostream>
#include <print>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>

#include "fe/loc.h"
#include "fe/utf8.h"

namespace fe {

/// Wrap a callable `f(std::ostream&) -> std::ostream&` so it streams via `operator<<` and `std::format`.
/// Useful for inline ad-hoc formatting:
/// ```
/// auto greet = fe::StreamFn{[](std::ostream& os) { os << "hi"; }};
/// std::cout << greet;
/// std::format("{}", greet);
/// ```
template<class F>
requires std::invocable<const F&, std::ostream&>
class StreamFn {
public:
    constexpr StreamFn(F f)
        : f_(std::move(f)) {}

    friend std::ostream& operator<<(std::ostream& os, StreamFn const& s) {
        if constexpr (std::same_as<std::invoke_result_t<F const&, std::ostream&>, std::ostream&>)
            return std::invoke(s.f_, os);
        else
            return std::invoke(s.f_, os), os;
    }

private:
    F f_;
};

template<class F>
StreamFn(F) -> StreamFn<F>;

/// Make types that support ostream operators available for `std::format`.
/// Use like this:
/// ```
/// template<> struct std::formatter<T> : fe::ostream_formatter {};
/// ```
/// @sa [Stack Overflow](https://stackoverflow.com/a/75738462).
template<class Char>
struct basic_ostream_formatter : std::formatter<std::basic_string_view<Char>, Char> {
    template<class T, class FormatContext>
    auto format(T const& value, FormatContext& ctx) const {
        std::basic_stringstream<Char> ss;
        ss << value;
        return std::formatter<std::basic_string_view<Char>, Char>::format(ss.view(), ctx);
    }
};

using ostream_formatter = basic_ostream_formatter<char>;

/// Keeps track of indentation level during output
class Tab {
public:
    /// @name Construction
    ///@{
    Tab(const Tab&) = default;
    Tab(std::string_view tab = {"\t"}, int indent = 0)
        : tab_(tab)
        , indent_(indent) {
        assert(indent >= 0);
    }

    static Tab spaces() { return Tab(std::string_view("    ")); }
    ///@}

    /// @name Getters
    ///@{
    constexpr int indent() const noexcept { return indent_; }
    constexpr std::string_view tab() const noexcept { return tab_; }
    ///@}

    // clang-format off
    /// @name Creates a new Tab
    ///@{
    ///
    [[nodiscard]] Tab operator+(int indent) const noexcept { assert(indent >= 0);                      return {tab_, indent_ + indent}; }
    [[nodiscard]] Tab operator-(int indent) const noexcept { assert(indent >= 0 && indent_ >= indent); return {tab_, indent_ - indent}; }
    ///@}

    /// @name Modifies this Tab
    ///@{
    constexpr Tab& operator++() noexcept {                      ++indent_; return *this; }
    constexpr Tab& operator--() noexcept { assert(indent_ > 0); --indent_; return *this; }
    constexpr Tab& operator+=(int indent) noexcept { assert(indent >= 0);                      indent_ += indent; return *this; }
    constexpr Tab& operator-=(int indent) noexcept { assert(indent >= 0 && indent_ >= indent); indent_ -= indent; return *this; }
    ///@}
    // clang-format on

    friend std::ostream& operator<<(std::ostream& os, Tab tab) {
        for (int i = 0; i != tab.indent_; ++i)
            os << tab.tab_;
        return os;
    }

private:
    std::string_view tab_;
    int indent_ = 0;
};

template<class T, class CharT = char>
concept Formattable
    = requires(std::basic_format_context<std::back_insert_iterator<std::basic_string<CharT>>, CharT>& ctx, T const& v) {
          std::formatter<std::remove_cvref_t<T>, CharT>{}.format(v, ctx);
      };

/// Join elements of @p range with @p sep.
/// Use as a `std::format` or `operator<<` argument: `std::format("{}", fe::Join(v, ", "))`.
template<std::ranges::input_range R>
requires Formattable<std::remove_cvref_t<std::ranges::range_reference_t<std::views::all_t<R>>>>
class Join {
public:
    using View = std::views::all_t<R>;

    Join(R&& range, std::string_view sep = ", ")
        : range_(std::views::all(std::forward<R>(range)))
        , sep_(sep) {}

    const auto& range() const { return range_; }
    std::string_view sep() const { return sep_; }

    friend std::ostream& operator<<(std::ostream& os, const Join& j) {
        for (std::string_view sep{}; const auto& elem : j.range_) {
            os << sep << elem;
            sep = j.sep_;
        }
        return os;
    }

private:
    View range_;
    std::string_view sep_;
};

template<class R>
Join(R&&, std::string_view = ", ") -> Join<R>;

} // namespace fe

#ifndef DOXYGEN
template<class F>
struct std::formatter<fe::StreamFn<F>> : fe::ostream_formatter {};

template<class R>
struct std::formatter<fe::Join<R>> {
    using elem_t = std::remove_cvref_t<std::ranges::range_reference_t<std::views::all_t<R>>>;
    std::formatter<elem_t> elem_fmt;

    constexpr auto parse(std::format_parse_context& ctx) { return elem_fmt.parse(ctx); }

    template<class FormatContext>
    auto format(const fe::Join<R>& j, FormatContext& ctx) const {
        auto out = ctx.out();
        for (std::string_view sep = {}; const auto& elem : j.range()) {
            out = std::ranges::copy(sep, out).out;
            ctx.advance_to(out);
            out = elem_fmt.format(elem, ctx);
            ctx.advance_to(out);
            sep = j.sep();
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

#ifdef NDEBUG
#    define assertf(condition, ...)  \
        do {                         \
            (void)sizeof(condition); \
        } while (false)
#else
#    define assertf(condition, ...)                                                                      \
        do {                                                                                             \
            if (!(condition)) {                                                                          \
                std::println(std::cerr, "{}:{}: assertion `{}` failed", __FILE__, __LINE__, #condition); \
                std::println(std::cerr __VA_ARGS__);                                                     \
                fe::breakpoint();                                                                        \
            }                                                                                            \
        } while (false)
#endif
