#include "fe/bitset.h"

#include <iostream>

namespace fe {

std::ostream& operator<<(std::ostream& os, const Bitset& bitset) {
    os << '{';
    for (auto sep = ""; auto i : bitset) {
        os << sep << i;
        sep = ", ";
    }
    return os << '}';
}

void Bitset::dump() const { std::cout << *this << std::endl; }

} // namespace fe
