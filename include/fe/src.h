#pragma once

#include <cstdint>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef FE_ABSL
#    include <absl/container/node_hash_map.h>
#else
#    include <unordered_map>
#endif

#include "fe/loc.h"
#include "fe/utf8.h"

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
    Src(std::filesystem::path path, std::string buf)
        : path_(std::move(path))
        , buf_(std::move(buf)) {
        if (buf_.starts_with(utf8::Bom)) bom_ = (uint32_t)utf8::Bom.size();
        rows_.emplace_back(0);
        for (uint32_t i = 0, e = (uint32_t)buf_.size(); i != e; ++i)
            if (buf_[i] == '\n') rows_.emplace_back(i + 1);
    }

    /// @name Getters
    ///@{
    const std::filesystem::path& path() const { return path_; }
    std::string_view buf() const { return buf_; }
    /// The number of rows the file actually has.
    /// @note A trailing line terminator does *not* open one more, empty row - it ends the last one.
    uint32_t num_rows() const { return (uint32_t)rows_.size() - phantom_(); }
    Pos begin() const { return Pos(0); }
    Pos end() const { return Pos((uint32_t)buf_.size()); }
    bool contains(Pos pos) const { return pos && pos.off <= buf_.size(); }
    ///@}

    /// @name Resolve a Pos
    ///@{
    /// 1-based row and column @p pos sits at, or `{0, 0}` if @p pos does not belong to this file.
    /// The column counts code points, not bytes, and a leading utf8::Bom occupies none.
    /// @note Never names a row Src::num_rows does not count - see there.
    std::pair<uint32_t, uint32_t> rowcol(Pos pos) const {
        if (!contains(pos)) return {0, 0};
        auto row = (uint32_t)(std::ranges::upper_bound(rows_, pos.off) - rows_.begin());

        // @p pos is at the very end of a file that ends with a terminator. That is no row of its own
        // but one past the end of the last real one - which is where an `<end of file>` token points.
        if (row > num_rows()) return {num_rows(), (uint32_t)utf8::num_code_points(line(num_rows())) + 1};

        auto begin = row == 1 ? std::min(bom_, pos.off) : rows_[row - 1];
        return {row, (uint32_t)utf8::num_code_points(sub(begin, pos.off)) + 1};
    }

    uint32_t row(Pos pos) const { return rowcol(pos).first; }
    uint32_t col(Pos pos) const { return rowcol(pos).second; }

    /// Text of the 1-based @p row without its line terminator - or a leading utf8::Bom;
    /// empty if @p row is out of range.
    std::string_view line(uint32_t row) const {
        if (row == 0 || row > num_rows()) return {};
        auto begin = row == 1 ? bom_ : rows_[row - 1];
        auto end   = row == rows_.size() ? (uint32_t)buf_.size() : rows_[row] - 1;
        if (end > begin && buf_[end - 1] == '\r') --end;
        return sub(begin, end);
    }

    /// Start of the last code point before @p pos - the character a half-open Loc::end points *past*.
    Pos prev(Pos pos) const {
        auto end = std::min<size_t>(pos.off, buf_.size());
        if (end == 0) return Pos(0);

        auto i = end - 1;
        while (i != 0 && utf8::is_valid234(char8_t(buf_[i])) != char8_t(-1))
            --i;

        // Only trust the backward scan if that candidate really decodes up to `end`:
        // utf8::decode resynchronizes malformed input byte by byte and this must not disagree.
        auto j = i;
        utf8::decode(buf_, j);
        return Pos((uint32_t)(j == end ? i : end - 1));
    }
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
    std::pair<const Src*, bool> add(std::filesystem::path path, std::string buf) {
        auto k          = key(path);
        auto [i, fresh] = path2src_.try_emplace(std::move(k), std::move(path), std::move(buf));
        return {&i->second, fresh};
    }

    /// As above, but reads the content from @p path.
    /// @returns a `nullptr` Src if @p path cannot be opened.
    std::pair<const Src*, bool> add(std::filesystem::path path) {
        auto k = key(path);
        if (auto i = path2src_.find(k); i != path2src_.end()) return {&i->second, false};
        auto ifs = std::ifstream(path, std::ios::binary);
        if (!ifs) return {nullptr, false};
        auto [i, fresh] = path2src_.try_emplace(std::move(k), std::move(path), slurp(ifs));
        return {&i->second, fresh};
    }

    /// Reads all of @p is into a `std::string`.
    static std::string slurp(std::istream& is) {
        return is ? std::string(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>()) : std::string();
    }
    ///@}

    /// @name Lookup
    ///@{
    /// @returns `nullptr` if @p path has not been registered.
    /// @note Compares SrcMap::key%s, so a @p path that merely *spells* a registered file
    /// differently still finds it.
    const Src* lookup(const std::filesystem::path& path) const {
        auto i = path2src_.find(key(path));
        return i == path2src_.end() ? nullptr : &i->second;
    }

    /// The key @p path is interned under - absolute, symlink-free, and normalized.
    /// This is where "do these two paths name the same file?" is decided - once, upon SrcMap::add -
    /// so that every comparison afterwards is a plain Loc::src pointer comparison.
    /// @note Resolves symlinks and `.`/`..` as far as @p path exists on disk and normalizes the rest
    /// lexically. A relative @p path is resolved against the current working directory *now*.
    static std::filesystem::path key(const std::filesystem::path& path) {
        std::error_code ec;
        // Absolute first: weakly_canonical only resolves the prefix of `path` that exists on disk,
        // and whether `foo` has such a prefix at all depends on it being spelled `./foo` or not.
        auto abs = std::filesystem::absolute(path, ec);
        if (ec) return path.lexically_normal();
        auto res = std::filesystem::weakly_canonical(abs, ec);
        return ec ? abs.lexically_normal() : res;
    }
    ///@}

private:
    PathMap<Src> path2src_; ///< Keyed by SrcMap::key; node-based, so a Src never moves.
};

} // namespace fe
