#pragma once

#include <cstdint>

#include <functional>
#include <string>

#include "fe/dbg.h"
#include "fe/src.h"
#include "fe/sym.h"
#include "fe/vector.h"

namespace fe {

/// Use/derive from this class for "global" variables that you need all over the place.
/// Well, there are not really global - that's the point of this class.
/// It manages a SymPool, a SrcMap, the interned Dbg%s, and how an Error lays out and renders a diagnostic.
/// @note Deliberately free of virtual functions: a polymorphic Driver has a vtable, and a vtable exported
/// from a shared library is a *data* symbol that Windows only resolves through `__declspec(dllimport)`.
struct Driver : public SymPool {
public:
    Driver()                  = default;
    Driver(Driver&&)          = default;
    ~Driver()                 = default;
    Driver(const Driver&)     = delete;
    Driver& operator=(Driver) = delete;

    /// @name Diagnostics
    /// @see fe::Error
    ///@{
    /// How an Error lays out - and how much of it it keeps.
    struct Diag {
        uint32_t gutter     = 5;     ///< Width of the line-number column.
        uint32_t max_rows   = 8;     ///< Rows a Snippet streams before eliding its middle; `0` elides nothing.
        uint32_t max_errors = 0;     ///< Errors recorded before the rest is dropped; `0` keeps everything.
        bool no_snippet     = false; ///< If `true`, a diagnostic is only its header line.
        bool werror         = false; ///< If `true`, a warning is recorded as an error.
    };

    /// Renders the text of one Error::Msg; a formatter is handed in and its result returned.
    /// Leave unset to simply invoke it; set it to postprocess the result - or to invoke it a second time,
    /// e.g. once the first pass turns out to have rendered two distinct entities under the same name.
    /// @warning The formatter captures its arguments by reference and is only valid for that one call.
    /// @warning A hook that captures its Driver does not survive a move; keep the Driver pinned.
    using Render = std::function<std::string(const std::function<std::string()>&)>;

    Diag diag;
    Render render;
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
    SrcMap src_;
    Vector<Dbg> dbgs_         = {Dbg()}; ///< Key `0` is the empty Dbg.
    DbgMap<uint32_t> dbg2key_ = {
        {Dbg(), 0}
    };
};

} // namespace fe
