#pragma once

#include <bit>
#include <ostream>

#ifdef FE_ABSL
#    include <absl/container/flat_hash_map.h>
#    include <absl/container/flat_hash_set.h>
#else
#    include <unordered_map>
#    include <unordered_set>
#endif

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
    constexpr Sym sym() const noexcept { return sym_; }
    constexpr Loc loc() const noexcept { return loc_; }
    /// Assumes `_` as the anonymous name.
    constexpr bool is_anon() const noexcept { return !sym() || sym() == '_'; }
    constexpr explicit operator bool() const noexcept { return sym().operator bool(); }
    ///@}

    /// @name Setters
    ///@{
    constexpr Dbg& set(Sym sym) noexcept { return sym_ = sym, *this; }
    constexpr Dbg& set(Loc loc) noexcept { return loc_ = loc, *this; }
    ///@}

    /// @name Comparison and Hashing
    ///@{
    /// @note Like Loc::operator==, this only compares Loc::src by pointer identity.
    constexpr bool operator==(const Dbg& other) const noexcept { return loc_ == other.loc_ && sym_ == other.sym_; }

    struct Hash {
        size_t operator()(Dbg dbg) const noexcept {
            auto h = hash_begin(std::bit_cast<uintptr_t>(dbg.loc_.src));
            h      = hash_combine(h, dbg.loc_.begin.off);
            h      = hash_combine(h, dbg.loc_.end.off);
            return hash_combine(h, Sym::Hash()(dbg.sym_));
        }
    };

    struct Eq {
        constexpr bool operator()(Dbg d1, Dbg d2) const noexcept { return d1 == d2; }
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

/// @name DbgMap/DbgSet
///@{
#ifdef FE_ABSL
template<class V>
using DbgMap = absl::flat_hash_map<Dbg, V, Dbg::Hash, Dbg::Eq>;
using DbgSet = absl::flat_hash_set<Dbg, Dbg::Hash, Dbg::Eq>;
#else
template<class V>
using DbgMap = std::unordered_map<Dbg, V, Dbg::Hash, Dbg::Eq>;
using DbgSet = std::unordered_set<Dbg, Dbg::Hash, Dbg::Eq>;
#endif
///@}

/// Opaque handle to a Dbg interned in a Driver; see Driver::dbg.
/// Handing one node another's key copies the handle verbatim: no Dbg is materialised and nothing is
/// looked up in the Driver's table.
/// @warning A key is only meaningful within the Driver that interned it.
class DbgKey {
public:
    constexpr DbgKey() noexcept = default; ///< The empty Dbg.

    constexpr explicit operator bool() const noexcept { return key_ != 0; } ///< Not the empty Dbg?
    constexpr bool operator==(const DbgKey&) const noexcept = default;

private:
    constexpr explicit DbgKey(uint32_t key) noexcept
        : key_(key) {}

    uint32_t key_ = 0;

    friend struct Driver;
};

} // namespace fe

#ifndef DOXYGEN
template<>
struct std::formatter<fe::Dbg> : fe::ostream_formatter {};
#endif
