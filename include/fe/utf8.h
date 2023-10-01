#pragma once

namespace fe::utf8 {

static constexpr size_t Max   = 4;      ///< Maximal number of `char8_t`s of an UTF-8 byte sequence.
static constexpr char32_t BOM = 0xfeff; ///< [Byte Order Mark](https://en.wikipedia.org/wiki/Byte_order_mark#UTF-8).
static constexpr char32_t EoF = (char32_t)std::istream::traits_type::eof(); ///< End of File.

/// Returns the expected number of bytes for an UTF-8 char sequence by inspecting the first byte.
/// Retuns @c 0 if invalid.
size_t num_bytes(char8_t c) {
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
inline char8_t is_valid234(char8_t c) { return (c & char8_t(0b11000000)) == char8_t(0b10000000) ? (c & char8_t(0b00111111)) : char8_t(-1); }

/// Decodes the next sequence of bytes from @p is as UTF-32.
/// @returns `0` on error.
inline char32_t decode(std::istream& is) {
    char32_t result = is.get();
    if (result == EoF) return result;

    switch (auto n = utf8::num_bytes(result)) {
        case 0: return 0;
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

} // namespace fe::utf8
