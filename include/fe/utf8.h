#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string_view>

#include "fe/assert.h"

/// UTF-8 helpers for decoding byte streams, encoding `char32_t` values, and running
/// ASCII-style character classification on `char32_t`.
///
/// The central entry points are @ref decode and @ref encode. Decoding returns
/// sentinel values such as @ref EoF and @ref Invalid instead of throwing.
namespace fe::utf8 {

static constexpr size_t Max   = 4;      ///< Maximal number of `char8_t`s of an UTF-8 byte sequence.
static constexpr char32_t BOM = 0xfeff; ///< [Byte Order Mark](https://en.wikipedia.org/wiki/Byte_order_mark#UTF-8).
static constexpr std::string_view Bom = "\xef\xbb\xbf"; ///< BOM as UTF-8 bytes.
static constexpr char32_t EoF
    = (char32_t)std::istream::traits_type::eof(); ///< End of stream sentinel returned by @ref decode.
static constexpr char32_t Null    = 0;            ///< U+0000 NULL returned unchanged by @ref decode.
static constexpr char32_t Invalid = 0x110000;     ///< Sentinel returned by @ref decode for malformed UTF-8.

/// Returns the expected number of bytes for an UTF-8 char sequence by inspecting the first byte.
/// Retuns @c 0 if invalid.
constexpr size_t num_bytes(char8_t c) noexcept {
    if ((c & char8_t(0b10000000)) == char8_t(0b00000000)) return 1;
    if ((c & char8_t(0b11100000)) == char8_t(0b11000000)) return 2;
    if ((c & char8_t(0b11110000)) == char8_t(0b11100000)) return 3;
    if ((c & char8_t(0b11111000)) == char8_t(0b11110000)) return 4;
    return 0;
}

/// Append @p b to @p c for converting UTF-8 to UTF-32.
constexpr char32_t append(char32_t c, char8_t b) noexcept { return (c << 6) | (b & 0b00111111); }

/// Get relevant bits of first UTF-8 byte @p c of a @em multi-byte sequence consisting of @p num bytes.
constexpr char32_t first(char32_t c, char32_t num) noexcept { return c & (0b00011111 >> (num - 2)); }

/// Minimum Unicode scalar value representable in an UTF-8 sequence of @p num bytes.
constexpr char32_t min_code_point(size_t num) noexcept {
    switch (num) {
        case 1: return 0x000000;
        case 2: return 0x000080;
        case 3: return 0x000800;
        case 4: return 0x010000;
        default: return 0x110000;
    }
}

/// Is @p c a valid Unicode scalar value?
constexpr bool is_scalar_value(char32_t c) noexcept { return c <= 0x10ffff && !(0xd800 <= c && c <= 0xdfff); }

/// Is the 2nd, 3rd, or 4th byte of an UTF-8 byte sequence valid?
/// @returns the extracted `char8_t` or `char8_t(-1)` if invalid.
constexpr char8_t is_valid234(char8_t c) noexcept {
    return (c & char8_t(0b11000000)) == char8_t(0b10000000) ? (c & char8_t(0b00111111)) : char8_t(-1);
}

/// Decodes the next UTF-8 sequence from @p is into a single `char32_t`.
///
/// Returns @ref EoF when the stream is exhausted and @ref Invalid for malformed,
/// overlong, surrogate, or otherwise non-scalar encodings.
inline char32_t decode(std::istream& is) {
    char32_t result = is.get();
    if (result == EoF) return result;

    switch (auto n = utf8::num_bytes(char8_t(result))) {
        case 0: return Invalid;
        case 1: return result;
        default:
            result = utf8::first(result, n);

            for (size_t i = 1; i != n; ++i)
                if (auto x = is_valid234(is.get()); x != char8_t(-1))
                    result = utf8::append(result, x);
                else
                    return Invalid;

            if (result < utf8::min_code_point(n) || !utf8::is_scalar_value(result)) return Invalid;
    }

    return result;
}

/// Decodes the UTF-8 sequence at @p i in @p str and advances @p i past it.
///
/// Returns @ref EoF at the end of @p str - leaving @p i alone - and @ref Invalid for malformed,
/// overlong, surrogate, or otherwise non-scalar encodings.
/// @note An @ref Invalid sequence advances @p i by a *single* byte, so the next @ref decode resynchronizes
/// instead of swallowing bytes that may well start a valid sequence themselves.
inline char32_t decode(std::string_view str, size_t& i) noexcept {
    if (i >= str.size()) return EoF;

    auto c8         = char8_t(str[i]);
    auto n          = utf8::num_bytes(c8);
    char32_t result = char32_t(c8);
    if (n == 0 || i + n > str.size()) return ++i, Invalid;
    if (n == 1) return ++i, result;

    result = utf8::first(result, n);
    for (size_t j = 1; j != n; ++j) {
        auto x = is_valid234(char8_t(str[i + j]));
        if (x == char8_t(-1)) return ++i, Invalid;
        result = utf8::append(result, x);
    }

    if (result < utf8::min_code_point(n) || !utf8::is_scalar_value(result)) return ++i, Invalid;

    i += n;
    return result;
}

/// Number of UTF-8 code points in @p str.
/// @note A column is counted in code points, so this is what turns a byte offset into one.
/// Counts via @ref decode, so a malformed sequence resynchronizes the same way the lexer does
/// and a @p str truncated mid-sequence counts its final partial one.
size_t num_code_points(std::string_view str) noexcept;

/// Encodes @p c32 as UTF-8 and writes the resulting bytes to @p os.
///
/// Returns `false` when @p c32 is outside the encodable range.
bool encode(std::ostream& os, char32_t c32);
/// Wrapper for `char32_t` with an `operator<<` that writes UTF-8.
struct Char32 {
    constexpr Char32(char32_t c) noexcept
        : c(c) {}

    friend std::ostream& operator<<(std::ostream& os, Char32 c) {
        auto res = utf8::encode(os, c.c);
        assert_unused(res);
        return os;
    }

    char32_t c;
};

/// @name Character classification
/// `char32_t`-style counterparts of the <[ctype](https://en.cppreference.com/w/cpp/header/cctype)>
/// functions, for a code point of any width - everything above U+00FF belongs to no class.
///
/// These do *not* call into `<cctype>`. Those are locale-dependent, so what counts as a letter would
/// shift under someone else's `std::setlocale` - no way to run a lexer. They are also out-of-line
/// calls through a locale table, which is a lot of ceremony for a question about one byte. What is
/// baked into the table below is the `"C"` locale, and it stays that way.
///@{
namespace detail {

enum Class : uint8_t {
    Cntrl  = 1 << 0,
    Space  = 1 << 1,
    Blank  = 1 << 2,
    Digit  = 1 << 3,
    Lower  = 1 << 4,
    Upper  = 1 << 5,
    Punct  = 1 << 6,
    XDigit = 1 << 7,
    Alpha  = Lower | Upper,
    Alnum  = Alpha | Digit,
    Graph  = Alnum | Punct,
};

/// The class of every byte, as the `"C"` locale has it; U+0080 and up belong to none.
inline constexpr auto Classes = [] {
    std::array<uint8_t, 256> res{};
    auto set = [&res](char32_t lo, char32_t hi, uint8_t bits) {
        for (auto c = lo; c <= hi; ++c) res[c] |= bits;
    };

    set(0x00, 0x1F, Cntrl);
    set(0x7F, 0x7F, Cntrl);
    set('\t', '\r', Space); // \t \n \v \f \r
    set('\t', '\t', Blank);
    set(' ', ' ', Space | Blank);
    set('0', '9', Digit | XDigit);
    set('A', 'F', XDigit);
    set('a', 'f', XDigit);
    set('A', 'Z', Upper);
    set('a', 'z', Lower);
    set('!', '/', Punct);
    set(':', '@', Punct);
    set('[', '`', Punct);
    set('{', '~', Punct);
    return res;
}();

/// Does @p c carry any of @p bits?
constexpr bool isa(char32_t c, uint8_t bits) noexcept { return (c & ~0xFF) == 0 && (Classes[c] & bits) != 0; }

} // namespace detail

// clang-format off
constexpr bool isalnum (char32_t c) noexcept { return detail::isa(c, detail::Alnum ); }
constexpr bool isalpha (char32_t c) noexcept { return detail::isa(c, detail::Alpha ); }
constexpr bool isblank (char32_t c) noexcept { return detail::isa(c, detail::Blank ); }
constexpr bool iscntrl (char32_t c) noexcept { return detail::isa(c, detail::Cntrl ); }
constexpr bool isdigit (char32_t c) noexcept { return detail::isa(c, detail::Digit ); }
constexpr bool isgraph (char32_t c) noexcept { return detail::isa(c, detail::Graph ); }
constexpr bool islower (char32_t c) noexcept { return detail::isa(c, detail::Lower ); }
constexpr bool ispunct (char32_t c) noexcept { return detail::isa(c, detail::Punct ); }
constexpr bool isspace (char32_t c) noexcept { return detail::isa(c, detail::Space ); }
constexpr bool isupper (char32_t c) noexcept { return detail::isa(c, detail::Upper ); }
constexpr bool isxdigit(char32_t c) noexcept { return detail::isa(c, detail::XDigit); }
constexpr bool isprint (char32_t c) noexcept { return c == ' ' || isgraph(c); }
constexpr bool isascii (char32_t c) noexcept { return c <= 0x7F; }
constexpr char32_t tolower(char32_t c) noexcept { return isupper(c) ? c - 'A' + 'a' : c; }
constexpr char32_t toupper(char32_t c) noexcept { return islower(c) ? c - 'a' + 'A' : c; }

/// Is @p c within [begin, finis]?
constexpr bool isrange(char32_t c, char32_t begin, char32_t finis) noexcept { return begin <= c && c <= finis; }
constexpr auto isrange(char32_t begin, char32_t finis) noexcept { return [=](char32_t c) { return isrange(c, begin, finis); }; }

constexpr bool isodigit(char32_t c) noexcept { return isrange(c, '0', '7'); } ///< Is octal digit?
constexpr bool isbdigit(char32_t c) noexcept { return isrange(c, '0', '1'); } ///< Is binary digit?
// clang-format on
///@}

/// @name any
/// Build a predicate that checks whether a code point matches any of the given values.
///@{
constexpr bool _any(char32_t c, char32_t d) noexcept { return c == d; }
template<class... T>
constexpr bool _any(char32_t c, char32_t d, T... args) noexcept {
    return c == d || _any(c, args...);
}
template<class... T>
constexpr auto any(T... args) noexcept {
    return [=](char32_t c) { return _any(c, args...); };
}
///@}

} // namespace fe::utf8
