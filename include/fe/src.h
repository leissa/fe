#pragma once

#include <cstdint>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fe/format.h"
#include "fe/loc.h"
#include "fe/utf8.h"

namespace fe {

/// The content of one source file together with the offsets its rows start at.
/// This is what turns a Pos back into the row/column a human wants to read.
class SrcFile {
public:
    SrcFile(std::filesystem::path path, std::string buf)
        : path_(std::move(path))
        , buf_(std::move(buf)) {
        rows_.emplace_back(0);
        for (uint32_t i = 0, e = (uint32_t)buf_.size(); i != e; ++i)
            if (buf_[i] == '\n') rows_.emplace_back(i + 1);
    }

    /// @name Getters
    ///@{
    const std::filesystem::path* path() const { return &path_; }
    std::string_view buf() const { return buf_; }
    uint32_t num_rows() const { return (uint32_t)rows_.size(); }
    Pos begin() const { return Pos(0); }
    Pos end() const { return Pos((uint32_t)buf_.size()); }
    bool contains(Pos pos) const { return pos && pos.off <= buf_.size(); }
    ///@}

    /// @name Resolve a Pos
    ///@{
    /// 1-based row and column @p pos sits at, or `{0, 0}` if @p pos does not belong to this file.
    /// The column counts code points, not bytes.
    std::pair<uint32_t, uint32_t> rowcol(Pos pos) const {
        if (!contains(pos)) return {0, 0};
        auto row = (uint32_t)(std::ranges::upper_bound(rows_, pos.off) - rows_.begin());
        return {row, (uint32_t)utf8::num_code_points(sub(rows_[row - 1], pos.off)) + 1};
    }

    uint32_t row(Pos pos) const { return rowcol(pos).first; }
    uint32_t col(Pos pos) const { return rowcol(pos).second; }

    /// Text of the 1-based @p row without its line terminator; empty if @p row is out of range.
    std::string_view line(uint32_t row) const {
        if (row == 0 || row > rows_.size()) return {};
        auto begin = rows_[row - 1];
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
    std::string_view sub(uint32_t begin, uint32_t end) const {
        return std::string_view(buf_).substr(begin, end - begin);
    }

    std::filesystem::path path_;
    std::string buf_;
    std::vector<uint32_t> rows_; ///< Offset each row starts at; `rows_.front() == 0`.
};

/// Owns the text - and the `std::filesystem::path` - of every file a Loc may point into.
/// Keep one in your Driver: a Loc is only as good as the SrcMap that can still resolve it.
class SrcMap {
public:
    /// @name Register a File
    ///@{
    /// Registers @p path with @p buf as its content and reports whether it is fresh.
    /// A @p path equivalent to an already registered one yields that entry instead.
    std::pair<const SrcFile*, bool> add(std::filesystem::path path, std::string buf) {
        if (auto file = lookup(path)) return {file, false};
        return {&files_.emplace_back(std::move(path), std::move(buf)), true};
    }

    /// As above, but reads the content from @p path.
    /// @returns a `nullptr` SrcFile if @p path cannot be opened.
    std::pair<const SrcFile*, bool> add(std::filesystem::path path) {
        if (auto file = lookup(path)) return {file, false};
        auto ifs = std::ifstream(path, std::ios::binary);
        if (!ifs) return {nullptr, false};
        return {&files_.emplace_back(std::move(path), slurp(ifs)), true};
    }

    /// Reads all of @p is into a `std::string`.
    static std::string slurp(std::istream& is) {
        return is ? std::string(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>()) : std::string();
    }
    ///@}

    /// @name Lookup
    /// Both yield `nullptr` if the file has not been registered.
    ///@{
    /// @note Compares lexically first, so this also works for a @p path that does not exist on disk.
    const SrcFile* lookup(const std::filesystem::path& path) const {
        std::error_code ignore;
        for (const auto& file : files_)
            if (*file.path() == path || (std::filesystem::equivalent(*file.path(), path, ignore) && !ignore))
                return &file;
        return nullptr;
    }

    /// @note Loc::path is owned by this SrcMap, so this compares pointers - no `fs::equivalent`.
    const SrcFile* lookup(Loc loc) const {
        if (loc.path)
            for (const auto& file : files_)
                if (file.path() == loc.path) return &file;
        return nullptr;
    }
    ///@}

    /// @name Render a Loc
    ///@{
    /// Streams @p loc as `file:row:col-row:col`, falling back to Loc%'s raw offsets if it cannot be resolved.
    /// The trailing position is the *last* character of @p loc - not the one Loc::end points past.
    std::ostream& stream(std::ostream& os, Loc loc) const {
        auto file = lookup(loc);
        if (!file || !file->contains(loc.begin) || !file->contains(loc.end) || loc.end < loc.begin) return os << loc;

        auto stream_pos = [&](Pos pos) {
            auto [row, col] = file->rowcol(pos);
            os << row << ':' << col;
        };
        os << file->path()->string() << ':';
        stream_pos(loc.begin);
        if (auto last = file->prev(loc.end); loc.begin < last) os << '-', stream_pos(last);
        return os;
    }

    /// Wraps @p loc together with this SrcMap so that it streams/formats as `file:row:col-row:col`.
    struct At {
        const SrcMap& map;
        Loc loc;

        friend std::ostream& operator<<(std::ostream& os, At at) { return at.map.stream(os, at.loc); }
    };

    At at(Loc loc) const { return {*this, loc}; }
    ///@}

private:
    std::deque<SrcFile> files_; ///< A `std::deque` so that SrcFile::path stays put.
};

} // namespace fe

#ifndef DOXYGEN
template<>
struct std::formatter<fe::SrcMap::At> : fe::ostream_formatter {};
#endif
