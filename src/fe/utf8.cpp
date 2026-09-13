#include "fe/utf8.h"

#include <ostream>

namespace fe::utf8 {

namespace {
// and, or
std::ostream& ao(std::ostream& os, char32_t c32, char32_t a = 0b00111111, char32_t o = 0b10000000) {
    return os << char((c32 & a) | o);
}
} // namespace

size_t num_code_points(std::string_view str) noexcept {
    size_t res = 0;
    for (size_t i = 0, e = str.size(); i < e; ++res)
        decode(str, i);
    return res;
}

bool encode(std::ostream& os, char32_t c32) {
    // clang-format off
    if (c32 <= 0x00007f) {          ao(os, c32      , 0b11111111, 0b00000000);                              return true; }
    if (c32 <= 0x0007ff) {       ao(ao(os, c32 >>  6, 0b00011111, 0b11000000),                        c32); return true; }
    if (c32 <= 0x00ffff) {    ao(ao(ao(os, c32 >> 12, 0b00001111, 0b11100000),             c32 >> 6), c32); return true; }
    if (c32 <= 0x10ffff) { ao(ao(ao(ao(os, c32 >> 18, 0b00000111, 0b11110000), c32 >> 12), c32 >> 6), c32); return true; }
    // clang-format on
    return false;
}

} // namespace fe::utf8
