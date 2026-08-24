#pragma once
// Default `Pos`/`Loc` stream output and dump helpers.
// Include this header in exactly one translation unit, or provide your own definitions instead.

#include "fe/format.h"
#include "fe/loc.h"

namespace fe {

std::ostream& operator<<(std::ostream& os, Pos pos) {
    if (pos) return os << pos.off;
    return os << "<unknown position>";
}

// A Pos is a byte offset, so this cannot spell out a row and column - `@` marks the raw offsets as such.
// Stream `SrcMap::at(loc)` instead wherever a human reads the result.
std::ostream& operator<<(std::ostream& os, Loc loc) {
    if (loc) {
        os << (loc.path ? loc.path->string() : "<unknown file>") << '@' << loc.begin;
        if (loc.begin != loc.end) os << '-' << loc.end;
        return os;
    }
    return os << "<unknown location>";
}

void Pos::dump() const { std::cout << *this << std::endl; }
void Loc::dump() const { std::cout << *this << std::endl; }

} // namespace fe
