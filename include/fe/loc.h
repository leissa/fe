#pragma once

#include <cassert>
#include <cstdint>

#include <algorithm>
#include <iosfwd>

#include "fe/sym.h"

namespace fe {

class Src;

/// Byte offset into a Src; pass around as value.
/// A Pos%ition is just an index - fe::Src turns it back into the row/column a human wants to read.
/// @warning Files beyond 4GiB wrap around, and the largest offset doubles as the "invalid" sentinel.
struct Pos {
    static constexpr uint32_t Invalid = uint32_t(-1);

    constexpr Pos() = default; ///< Creates an invalid Pos%ition.
    constexpr explicit Pos(uint32_t off)
        : off(off) {}

    constexpr explicit operator bool() const { return off != Invalid; } ///< Is a valid Pos%ition?
    constexpr auto operator<=>(const Pos&) const = default;
    constexpr Pos operator+(uint32_t n) const {
        assert(*this && (uint64_t)off + n < Invalid);
        return Pos(off + n);
    }
    void dump() const;

    uint32_t off = Invalid;

    /// `fe/loc.h` only declares the stream output and dump helpers.
    /// Link `fe-lib` for the default implementation, or provide your own.
    friend std::ostream& operator<<(std::ostream& os, Pos pos);
};

/// Loc%ation within a Src: the half-open byte range `[Loc::begin, Loc::end)`.
/// It's only two machine words on a 64 bit arch, so pass around as value.
/// An *empty* range is a Loc%ation *between* two characters - what you want to point at
/// when something is missing rather than wrong; see Loc::anew_end.
/// @warning Loc::src is only a pointer and it is your job to guarantee that the underlying
/// fe::Src outlives this Loc%ation; fe::SrcMap owns one for you.
struct Loc {
    /// How much of a Loc a diagnostic spells out; see Diag::loc_style.
    enum class Style {
        Full,   ///< `path:row:col-row:col` - the whole range.
        RowCol, ///< `path:row:col`
        Row,    ///< `path:row`
        MSVC,   ///< `path(row,col)`
    };

    constexpr Loc() = default; ///< Creates an invalid Loc%ation.
    constexpr Loc(const Src* src, Pos begin, Pos end)
        : src(src)
        , begin(begin)
        , end(end) {}
    constexpr Loc(const Src* src, Pos pos)
        : Loc(src, pos, pos) {} ///< The empty Loc%ation at @p pos.
    constexpr Loc(Pos begin, Pos end)
        : Loc(nullptr, begin, end) {}
    constexpr Loc(Pos pos)
        : Loc(nullptr, pos, pos) {}

    constexpr Loc anew_begin() const { return {src, begin, begin}; }
    constexpr Loc anew_end() const { return {src, end, end}; }
    constexpr uint32_t size() const {
        assert((bool)begin == (bool)end && begin <= end);
        return end.off - begin.off;
    }
    constexpr Loc operator+(Pos pos) const { return {src, begin, pos}; }
    constexpr Loc operator+(Loc loc) const { return {src, begin, loc.end}; } ///< The hull of both Loc%ations.

    /// The overlap of both Loc%ations - invalid if they are disjoint or sit in different files.
    /// Dual to operator+; since an invalid Loc is falsy, `if (a & b)` also reads as "do they overlap?".
    /// An empty Loc overlaps nothing, not even itself.
    /// @note Loc::src is only compared via pointer equality - which is all it takes, as fe::SrcMap
    /// interns paths and hands out exactly one Src per file.
    constexpr Loc operator&(Loc loc) const {
        auto b = std::max(begin, loc.begin);
        auto e = std::min(end, loc.end);
        if (src != loc.src || b >= e) return {};
        return {src, b, e};
    }

    constexpr explicit operator bool() const { return (bool)begin; } ///< Is a valid Loc%ation?
    /// @note Loc::src is only checked via pointer equality.
    constexpr bool operator==(Loc other) const { return begin == other.begin && end == other.end && src == other.src; }
    void dump() const;

    const Src* src = {};
    Pos begin      = {};
    Pos end        = {};
    ///< It's called `end` because - just like an STL iterator - it refers to the byte
    /// one **past** the last one within this Loc%ation.

    /// Streams as `path:row:col-row:col`, resolved through Loc::src - or as raw offsets if it has none.
    /// `fe/loc.h` only declares the stream output and dump helpers.
    /// Link `fe-lib` for the default implementation, or provide your own.
    friend std::ostream& operator<<(std::ostream& os, Loc loc);
};

// Keep Loc at two machine words so it is passed/returned in registers.
static_assert(sizeof(void*) != 8 || sizeof(Loc) == 16, "Loc should stay two machine words");

} // namespace fe
