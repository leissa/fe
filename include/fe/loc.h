#pragma once

#include <cstdint>

#include <algorithm>
#include <filesystem>
#include <iosfwd>

#include "fe/sym.h"

namespace fe {

/// Byte offset into a source file; pass around as value.
/// A Pos%ition is just an index - fe::SrcFile turns it back into the row/column a human wants to read.
/// @warning Files beyond 4GiB wrap around, and the largest offset doubles as the "invalid" sentinel.
struct Pos {
    static constexpr uint32_t Invalid = uint32_t(-1);

    constexpr Pos() = default; ///< Creates an invalid Pos%ition.
    constexpr explicit Pos(uint32_t off)
        : off(off) {}

    constexpr explicit operator bool() const { return off != Invalid; } ///< Is a valid Pos%ition?
    constexpr auto operator<=>(const Pos&) const = default;
    constexpr Pos operator+(uint32_t n) const { return Pos(off + n); }
    void dump() const;

    uint32_t off = Invalid;

    /// `fe/loc.h` only declares the stream output and dump helpers.
    /// Include `fe/loc.cpp.h` in exactly one translation unit for the default implementation,
    /// or provide your own definitions instead.
    friend std::ostream& operator<<(std::ostream& os, Pos pos);
};

/// Loc%ation in a File: the half-open byte range `[Loc::begin, Loc::end)`.
/// It's only two machine words on a 64 bit arch, so pass around as value.
/// An *empty* range is a Loc%ation *between* two characters - what you want to point at
/// when something is missing rather than wrong; see Loc::anew_end.
/// @warning Loc::path is only a pointer and it is your job to guarantee that the underlying
/// `std::filesystem::path` outlives this Loc%ation; fe::SrcMap owns one for you.
struct Loc {
    constexpr Loc() = default; ///< Creates an invalid Loc%ation.
    constexpr Loc(const std::filesystem::path* path, Pos begin, Pos end)
        : path(path)
        , begin(begin)
        , end(end) {}
    constexpr Loc(const std::filesystem::path* path, Pos pos)
        : Loc(path, pos, pos) {} ///< The empty Loc%ation at @p pos.
    constexpr Loc(Pos begin, Pos end)
        : Loc(nullptr, begin, end) {}
    constexpr Loc(Pos pos)
        : Loc(nullptr, pos, pos) {}

    constexpr Loc anew_begin() const { return {path, begin, begin}; }
    constexpr Loc anew_end() const { return {path, end, end}; }
    constexpr uint32_t size() const { return end.off - begin.off; }
    constexpr Loc operator+(Pos pos) const { return {path, begin, pos}; }
    constexpr Loc operator+(Loc loc) const { return {path, begin, loc.end}; } ///< The hull of both Loc%ations.

    /// The overlap of both Loc%ations - invalid if they are disjoint or sit in different files.
    /// Dual to operator+; since an invalid Loc is falsy, `if (a & b)` also reads as "do they overlap?".
    /// An empty Loc overlaps nothing, not even itself.
    /// @note Loc::path is only compared via pointer equality, so two Loc%ations in the same file
    /// reached through different `std::filesystem::path` objects never overlap.
    constexpr Loc operator&(Loc loc) const {
        auto b = std::max(begin, loc.begin);
        auto e = std::min(end, loc.end);
        if (path != loc.path || b >= e) return {};
        return {path, b, e};
    }

    constexpr explicit operator bool() const { return (bool)begin; } ///< Is a valid Loc%ation?
    /// @note Loc::path is only checked via pointer equality.
    constexpr bool operator==(Loc other) const {
        return begin == other.begin && end == other.end && path == other.path;
    }
    void dump() const;

    const std::filesystem::path* path = {};
    Pos begin                         = {};
    Pos end                           = {};
    ///< It's called `end` because - just like an STL iterator - it refers to the byte
    /// one **past** the last one within this Loc%ation.

    /// `fe/loc.h` only declares the stream output and dump helpers.
    /// Include `fe/loc.cpp.h` in exactly one translation unit for the default implementation,
    /// or provide your own definitions instead.
    friend std::ostream& operator<<(std::ostream& os, Loc loc);
};

// Keep Loc at two machine words so it is passed/returned in registers.
static_assert(sizeof(void*) != 8 || sizeof(Loc) == 16, "Loc should stay two machine words");

} // namespace fe
