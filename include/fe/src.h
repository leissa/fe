#pragma once

#include <cstdint>

#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef FE_ABSL
#    include <absl/container/node_hash_map.h>
#else
#    include <unordered_map>
#endif

#include "fe/loc.h"

namespace fe {

/// Hashes a `std::filesystem::path` - consistent with its `operator==`, which compares lexically.
struct PathHash {
    size_t operator()(const std::filesystem::path& path) const noexcept { return std::filesystem::hash_value(path); }
};

/// Maps a `std::filesystem::path` to @p V.
/// @warning Node-based on purpose: fe::SrcMap stores its Src%s in here and a Loc points to one,
/// so the values must never move.
#ifdef FE_ABSL
template<class V>
using PathMap = absl::node_hash_map<std::filesystem::path, V, PathHash>;
#else
template<class V>
using PathMap = std::unordered_map<std::filesystem::path, V, PathHash>;
#endif

/// The content of one source file together with the offsets its rows start at.
/// This is what turns a Pos back into the row/column a human wants to read.
class Src {
public:
    Src(std::filesystem::path path, std::string buf);

    /// @name Getters
    ///@{
    const std::filesystem::path& path() const { return path_; }
    std::string_view buf() const { return buf_; }
    /// The number of rows the file actually has.
    /// @note A trailing line terminator does *not* open one more, empty row - it ends the last one.
    uint32_t num_rows() const;
    Pos begin() const { return Pos(0); }
    Pos end() const { return Pos((uint32_t)buf_.size()); }
    bool contains(Pos pos) const { return pos && pos.off <= buf_.size(); }
    ///@}

    /// @name Resolve a Pos
    ///@{
    /// 1-based row and column @p pos sits at, or `{0, 0}` if @p pos does not belong to this file.
    /// The column counts code points, not bytes, and a leading utf8::Bom occupies none.
    /// @note Never names a row Src::num_rows does not count - see there.
    std::pair<uint32_t, uint32_t> rowcol(Pos pos) const;

    uint32_t row(Pos pos) const { return rowcol(pos).first; }
    uint32_t col(Pos pos) const { return rowcol(pos).second; }

    /// Text of the 1-based @p row without its line terminator - or a leading utf8::Bom;
    /// empty if @p row is out of range.
    std::string_view line(uint32_t row) const;

    /// Start of the last code point before @p pos - the character a half-open Loc::end points *past*.
    Pos prev(Pos pos) const;
    ///@}

private:
    /// Scanning for `\n` appends one more offset when buf_ ends with a terminator: a row with
    /// nothing in it and nothing after it. A terminator *ends* its row rather than opening a new
    /// one, so that entry is an artifact of the scan and not a row the file has.
    uint32_t phantom_() const { return rows_.size() > 1 && rows_.back() == buf_.size() ? 1 : 0; }

    std::string_view sub(uint32_t begin, uint32_t end) const {
        return std::string_view(buf_).substr(begin, end - begin);
    }

    std::filesystem::path path_;
    std::string buf_;
    std::vector<uint32_t> rows_; ///< Offset each row starts at; `rows_.front() == 0`.
    uint32_t bom_ = 0;           ///< Byte size of a leading utf8::Bom, which is not a column.
};

/// Interns the text - and the `std::filesystem::path` - of every file a Loc may point into.
/// Keep one in your Driver: a Loc is only as good as the SrcMap that keeps its Src alive.
/// Each file lives here exactly once, so Loc::src identifies it by pointer - see SrcMap::key.
class SrcMap {
public:
    /// @name Register a File
    ///@{
    /// Registers @p path with @p buf as its content and reports whether it is fresh.
    /// A @p path with the same SrcMap::key as an already registered one yields that entry instead.
    std::pair<const Src*, bool> add(std::filesystem::path path, std::string buf);

    /// As above, but reads the content from @p path.
    /// @returns a `nullptr` Src if @p path cannot be opened.
    std::pair<const Src*, bool> add(std::filesystem::path path);

    /// Reads all of @p is into a `std::string`.
    static std::string slurp(std::istream& is);
    ///@}

    /// @name Lookup
    ///@{
    /// @returns `nullptr` if @p path has not been registered.
    /// @note Compares SrcMap::key%s, so a @p path that merely *spells* a registered file
    /// differently still finds it.
    const Src* lookup(const std::filesystem::path& path) const;

    /// The key @p path is interned under - absolute, symlink-free, and normalized.
    /// This is where "do these two paths name the same file?" is decided - once, upon SrcMap::add -
    /// so that every comparison afterwards is a plain Loc::src pointer comparison.
    /// @note Resolves symlinks and `.`/`..` as far as @p path exists on disk and normalizes the rest
    /// lexically. A relative @p path is resolved against the current working directory *now*.
    static std::filesystem::path key(const std::filesystem::path& path);
    ///@}

private:
    PathMap<Src> path2src_; ///< Keyed by SrcMap::key; node-based, so a Src never moves.
};

} // namespace fe
