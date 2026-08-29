#pragma once

#include <bit>
#include <ostream>

#include "fe/format.h"
#include "fe/hash.h"
#include "fe/loc.h"
#include "fe/sym.h"

namespace fe {

/// The debug info of an entity: where it came from and what it was called.
struct Dbg {
public:
    /// @name Constructors
    ///@{
    constexpr Dbg() noexcept           = default;
    constexpr Dbg(const Dbg&) noexcept = default;
    constexpr Dbg(Loc loc, Sym sym) noexcept
        : loc_(loc)
        , sym_(sym) {}
    constexpr Dbg(Loc loc) noexcept
        : Dbg(loc, {}) {}
    constexpr Dbg(Sym sym) noexcept
        : Dbg({}, sym) {}
    Dbg& operator=(const Dbg&) noexcept = default;
    ///@}

    /// @name Getters
    ///@{
    Sym sym() const { return sym_; }
    Loc loc() const { return loc_; }
    bool is_anon() const { return !sym() || sym() == '_'; } ///< Assumes `_` as the anonymous name.
    explicit operator bool() const { return sym().operator bool(); }
    ///@}

    /// @name Setters
    ///@{
    Dbg& set(Sym sym) { return sym_ = sym, *this; }
    Dbg& set(Loc loc) { return loc_ = loc, *this; }
    ///@}

    /// @name Comparison and Hashing
    ///@{
    /// @note Like Loc::operator==, this only compares Loc::src by pointer identity.
    bool operator==(const Dbg& other) const noexcept { return loc_ == other.loc_ && sym_ == other.sym_; }

    struct Hash {
        size_t operator()(Dbg dbg) const noexcept {
            auto h = hash_begin(std::bit_cast<uintptr_t>(dbg.loc_.src));
            h      = hash_combine(h, dbg.loc_.begin.off);
            h      = hash_combine(h, dbg.loc_.end.off);
            return hash_combine(h, Sym::Hash()(dbg.sym_));
        }
    };

    struct Eq {
        bool operator()(Dbg d1, Dbg d2) const noexcept { return d1 == d2; }
    };

#ifdef FE_ABSL
    template<class H>
    friend H AbslHashValue(H h, Dbg dbg) noexcept {
        return H::combine(std::move(h), dbg.loc_.src, dbg.loc_.begin.off, dbg.loc_.end.off, dbg.sym_);
    }
#endif
    ///@}

private:
    Loc loc_;
    Sym sym_;

    friend std::ostream& operator<<(std::ostream& os, const Dbg& dbg) { return os << dbg.sym(); }
};

} // namespace fe

#ifndef DOXYGEN
template<>
struct std::formatter<fe::Dbg> : fe::ostream_formatter {};
#endif
