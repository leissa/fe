#pragma once

#include <cctype>

#include <istream>
#include <ostream>

#include "fe/assert.h"

namespace fe::utf8 {

static constexpr size_t Max    = 4;      ///< Maximal number of `char8_t`s of an UTF-8 byte sequence.
static constexpr char32_t BOM  = 0xfeff; ///< [Byte Order Mark](https://en.wikipedia.org/wiki/Byte_order_mark#UTF-8).
static constexpr char32_t EoF  = (char32_t)std::istream::traits_type::eof(); ///< End of File.
static constexpr char32_t Null = 0;

/// Returns the expected number of bytes for an UTF-8 char sequence by inspecting the first byte.
/// Retuns @c 0 if invalid.
inline size_t num_bytes(char8_t c) {
    if ((c & char8_t(0b10000000)) == char8_t(0b00000000)) return 1;
    if ((c & char8_t(0b11100000)) == char8_t(0b11000000)) return 2;
    if ((c & char8_t(0b11110000)) == char8_t(0b11100000)) return 3;
    if ((c & char8_t(0b11111000)) == char8_t(0b11110000)) return 4;
    return 0;
}

/// Append @p b to @p c for converting UTF-8 to UTF-32.
inline char32_t append(char32_t c, char32_t b) { return (c << 6) | (b & 0b00111111); }

/// Get relevant bits of first UTF-8 byte @p c of a @em multi-byte sequence consisting of @p num bytes.
inline char32_t first(char32_t c, char32_t num) { return c & (0b00011111 >> (num - 2)); }

/// Is the 2nd, 3rd, or 4th byte of an UTF-8 byte sequence valid?
/// @returns the extracted `char8_t` or `char8_t(-1)` if invalid.
inline char8_t is_valid234(char8_t c) {
    return (c & char8_t(0b11000000)) == char8_t(0b10000000) ? (c & char8_t(0b00111111)) : char8_t(-1);
}

/// Decodes the next sequence of bytes from @p is as UTF-32.
/// @returns Null on error.
inline char32_t decode(std::istream& is) {
    char32_t result = is.get();
    if (result == EoF) return result;

    switch (auto n = utf8::num_bytes(result)) {
        case 0: return Null;
        case 1: return result;
        default:
            result = utf8::first(result, n);

            for (size_t i = 1; i != n; ++i)
                if (auto x = is_valid234(is.get()); x != char8_t(-1))
                    result = utf8::append(result, x);
                else
                    return 0;
    }

    return result;
}

namespace {
// and, or
std::ostream& ao(std::ostream& os, char32_t c32, char32_t a = 0b00111111, char32_t o = 0b10000000) {
    return os << char((c32 & a) | o);
}
} // namespace

/// Encodes the UTF-32 char @p c32 as UTF-8 and writes the sequence of bytes to @p os.
/// @returns `false` on error.
inline bool encode(std::ostream& os, char32_t c32) {
    // clang-format off
    if (c32 <= 0x00007f) {          ao(os, c32      , 0b11111111, 0b00000000);                              return true; }
    if (c32 <= 0x0007ff) {       ao(ao(os, c32 >>  6, 0b00011111, 0b11000000),                        c32); return true; }
    if (c32 <= 0x00ffff) {    ao(ao(ao(os, c32 >> 12, 0b00001111, 0b11100000),             c32 >> 6), c32); return true; }
    if (c32 <= 0x10ffff) { ao(ao(ao(ao(os, c32 >> 18, 0b00000111, 0b11110000), c32 >> 12), c32 >> 6), c32); return true; }
    // clang-format on
    return false;
}
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

/// @name Wrappers
///@{
/// Safe `char32_t`-style wrappers for <[ctype](https://en.cppreference.com/w/cpp/header/cctype)> functions:
/// > Like all other functions from `<cctype>`, the behavior of `std::isalnum` is undefined if the argument's value is
/// neither representable as `unsigned char` nor equal to `EOF`.
// clang-format off
inline bool isalnum (char32_t c) { return (c & ~0xFF) == 0 ? std::isalnum (c) : false; }
inline bool isalpha (char32_t c) { return (c & ~0xFF) == 0 ? std::isalpha (c) : false; }
inline bool isblank (char32_t c) { return (c & ~0xFF) == 0 ? std::isblank (c) : false; }
inline bool iscntrl (char32_t c) { return (c & ~0xFF) == 0 ? std::iscntrl (c) : false; }
inline bool isdigit (char32_t c) { return (c & ~0xFF) == 0 ? std::isdigit (c) : false; }
inline bool isgraph (char32_t c) { return (c & ~0xFF) == 0 ? std::isgraph (c) : false; }
inline bool islower (char32_t c) { return (c & ~0xFF) == 0 ? std::islower (c) : false; }
inline bool isprint (char32_t c) { return (c & ~0xFF) == 0 ? std::isprint (c) : false; }
inline bool ispunct (char32_t c) { return (c & ~0xFF) == 0 ? std::ispunct (c) : false; }
inline bool isspace (char32_t c) { return (c & ~0xFF) == 0 ? std::isspace (c) : false; }
inline bool isupper (char32_t c) { return (c & ~0xFF) == 0 ? std::isupper (c) : false; }
inline bool isxdigit(char32_t c) { return (c & ~0xFF) == 0 ? std::isxdigit(c) : false; }
inline bool isascii (char32_t c) { return c <= 0x7f; }
inline char32_t tolower(char32_t c) { return (c & ~0xFF) == 0 ? std::tolower(c) : c; }
inline char32_t toupper(char32_t c) { return (c & ~0xFF) == 0 ? std::toupper(c) : c; }

/// Is @p c within [begin, finis]?
inline bool _isrange(char32_t c, char32_t begin, char32_t finis) { return begin <= c && c <= finis; }
inline auto isrange(char32_t begin, char32_t finis) { return [=](char32_t c) { return _isrange(c, begin, finis); }; }

/// Is octal digit?
inline bool isodigit(char32_t c) { return _isrange(c, '0', '7'); }
inline bool isbdigit(char32_t c) { return _isrange(c, '0', '1'); }
// clang-format on
///@}

/// @name any
///@{
/// Checks whether @p c is any of the remaining arguments.
inline bool _any(char32_t c, char32_t d) { return c == d; }
template<class... T> inline bool _any(char32_t c, char32_t d, T... args) { return c == d || _any(c, args...); }
template<class... T> inline auto any(T... args) {
    return [=](char32_t c) { return _any(c, args...); };
}
///@}

} // namespace fe::utf8
