#pragma once
// Default `Pos`/`Loc` stream output and dump helpers.
// Include this header in exactly one translation unit, or provide your own definitions instead.

#include "fe/format.h"
#include "fe/loc.h"
#include "fe/src.h" // Loc::src is only forward-declared in fe/loc.h

namespace fe {

std::ostream& operator<<(std::ostream& os, Pos pos) {
    if (pos) return os << pos.off;
    return os << "<unknown position>";
}

// The trailing position is the *last* character of `loc`, not the one Loc::end points past.
std::ostream& operator<<(std::ostream& os, Loc loc) {
    if (!loc) return os << "<unknown location>";
    auto src = loc.src;

    if (src && src->contains(loc.begin) && src->contains(loc.end) && loc.begin <= loc.end) {
        auto stream_pos = [&](Pos pos) {
            auto [row, col] = src->rowcol(pos);
            os << row << ':' << col;
        };
        os << src->path().string() << ':';
        stream_pos(loc.begin);
        if (auto last = src->prev(loc.end); loc.begin < last) os << '-', stream_pos(last);
        return os;
    }

    os << (src ? src->path().string() : "<unknown file>") << '@' << loc.begin;
    if (loc.begin != loc.end) os << '-' << loc.end;
    return os;
}

void Pos::dump() const { std::cout << *this << std::endl; }
void Loc::dump() const { std::cout << *this << std::endl; }

} // namespace fe
