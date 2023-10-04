#pragma once

#include <filesystem>

#include "fe/config.h"
#include "fe/sym.h"

namespace fe {

/// Pos%ition in a source file; pass around as value.
struct Pos {
    Pos() = default; ///< Creates an invalid Pos&ition.
    Pos(uint16_t row)
        : row(row) {}
    Pos(uint16_t row, uint16_t col)
        : row(row)
        , col(col) {}

    explicit operator bool() const { return row != 0; } ///< Is a valid Pos&ition?
    auto operator<=>(const Pos&) const = default;
    void dump() const { std::cout << *this << std::endl; }

    uint16_t row = 0;
    uint16_t col = 0;

    /// Write your own implementation or include fe/loc.cpp.h somewhere for a default one.
    friend std::ostream& operator<<(std::ostream& os, const Pos pos);
};

/// Loc%ation in a File.
/// It's only two machine words on a 64 bit arch, so pass around as value.
/// @warning Loc::path is only a pointer and it is your job to guarantee
/// that the underlying `std::filesystem::path` outlives this Loc%ation.
struct Loc {
    Loc() = default; ///< Creates an invalid Loc%ation.
    Loc(const std::filesystem::path* path, Pos begin, Pos finis)
        : path(path)
        , begin(begin)
        , finis(finis) {}
    Loc(const std::filesystem::path* file, Pos pos)
        : Loc(file, pos, pos) {}
    Loc(Pos begin, Pos finis)
        : Loc(nullptr, begin, finis) {}
    Loc(Pos pos)
        : Loc(nullptr, pos, pos) {}

    Loc anew_begin() const { return {path, begin, begin}; }
    Loc anew_finis() const { return {path, finis, finis}; }
    Loc operator+(Loc loc) const { return {path, begin, loc.finis}; }
    explicit operator bool() const { return (bool)begin; } ///< Is a valid Loc%ation?
    /// @note Loc::path is only checked via pointer equality.
    bool operator==(Loc other) const { return begin == other.begin && finis == other.finis && path == other.path; }
    void dump() const { std::cout << *this << std::endl; }

    const std::filesystem::path* path = {};
    Pos begin                         = {};
    Pos finis                         = {};
    ///< It's called `finis` because it refers to the **last** character within this Loc%ation.
    /// In the STL the word `end` refers to the position of something that is one element **past** the end.

    /// Write your own implementation or include fe/loc.cpp.h somewhere for a default one.
    friend std::ostream& operator<<(std::ostream& os, const Loc loc);
};

} // namespace fe
