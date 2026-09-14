#include "fe/sym.h"

#include <algorithm>
#include <ostream>

namespace fe {

std::ostream& operator<<(std::ostream& os, Sym sym) { return os << sym.view(); }

Sym SymPool::sym(std::string_view s) {
    if (s.empty()) return Sym();
    auto size = s.size();

    if (size <= Sym::Short_String_Bytes - 1) { // small string: need one more byte for the size
        uintptr_t ptr = size;
        // Little endian: 2 a b ? register: ?ba2
        // Big endian:    a b ? 2 register: ab?2
        // `char` is signed here, so a byte >= 0x80 would sign-extend and flood every higher byte.
        if constexpr (std::endian::native == std::endian::little)
            for (uintptr_t i = 0, shift = 8; i != size; ++i, shift += 8)
                ptr |= (uintptr_t(uint8_t(s[i])) << shift);
        else
            for (uintptr_t i = 0, shift = (Sym::Short_String_Bytes - 1) * 8; i != size; ++i, shift -= 8)
                ptr |= (uintptr_t(uint8_t(s[i])) << shift);
        return Sym(ptr);
    }

    auto state = strings_.state();
    auto ptr   = (String*)strings_.allocate(sizeof(String) + s.size(), Sym::Short_String_Bytes);
    new (ptr) String(s.size());
    std::copy(s.begin(), s.end(), ptr->chars);
    auto [i, ins] = pool_.emplace(ptr);
    if (ins) return Sym(std::bit_cast<uintptr_t>(ptr));
    strings_.deallocate(state);
    return Sym(std::bit_cast<uintptr_t>(*i));
}

} // namespace fe
