#pragma once

#include <cassert>

#include <memory>
#include <utility>

#include "fe/dbg.h"
#include "fe/diag.h"
#include "fe/src.h"
#include "fe/sym.h"
#include "fe/vector.h"

namespace fe {

/// Use/derive from this class for "global" variables that you need all over the place.
/// Well, there are not really global - that's the point of this class.
/// It manages a SymPool, a SrcMap, the interned Dbg%s, and the Diag that lays out a diagnostic.
/// @note Deliberately free of virtual functions - Driver::diag is where you plug in behavior of your own.
struct Driver : public SymPool {
public:
    Driver();
    Driver(Driver&&) = default;
    ~Driver();
    Driver(const Driver&)     = delete;
    Driver& operator=(Driver) = delete;

    /// @name Diagnostics
    /// @see fe::Error
    ///@{
    Diag& diag() { return *diag_; }
    const Diag& diag() const { return *diag_; }

    /// Installs @p diag - a subclass of your own, typically - and yields the previous one.
    /// @warning Never `nullptr`.
    std::unique_ptr<Diag> diag(std::unique_ptr<Diag> diag) {
        assert(diag && "a Driver always has a Diag");
        return std::exchange(diag_, std::move(diag));
    }
    ///@}

    /// @name Source Files
    /// Register every file you lex here: a Loc is only as good as the SrcMap that keeps its Src alive.
    ///@{
    SrcMap& src() { return src_; }
    const SrcMap& src() const { return src_; }
    ///@}

    /// @name Dbg Interning
    /// A node only has to store the DbgKey instead of a full Dbg.
    /// Both halves of a Dbg are already owned here:
    /// Dbg::sym is interned in this Driver's SymPool and Dbg::loc points into Driver::src.
    ///@{
    Dbg dbg(DbgKey key) const { return dbgs_[key.key_]; }

    /// Interns @p dbg and yields its DbgKey.
    DbgKey dbg(Dbg dbg) {
        if (auto i = dbg2key_.find(dbg); i != dbg2key_.end()) return DbgKey(i->second);
        auto key = uint32_t(dbgs_.size());
        dbgs_.emplace_back(dbg);
        dbg2key_.emplace(dbg, key);
        return DbgKey(key);
    }
    ///@}

private:
    std::unique_ptr<Diag> diag_;
    SrcMap src_;
    Vector<Dbg> dbgs_         = {Dbg()}; ///< Key `0` is the empty Dbg.
    DbgMap<uint32_t> dbg2key_ = {
        {Dbg(), 0}
    };
};

} // namespace fe
