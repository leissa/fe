#include "fe/format.h"
#include "fe/loc.h"

namespace fe {

std::ostream& operator<<(std::ostream& os, const Pos pos) {
    if (pos.row) {
        if (pos.col) return os << pos.row << ':' << pos.col;
        return os << pos.row;
    }
    return os << "<unknown position>";
}

std::ostream& operator<<(std::ostream& os, const Loc loc) {
    if (loc) {
        os << (loc.path ? loc.path->string() : "<unknown file>") << ':' << loc.begin;
        if (loc.begin != loc.finis) os << '-' << loc.finis;
        return os;
    }
    return os << "<unknown location>";
}

} // namespace fe
