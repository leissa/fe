#include "fe/src.h"

#include <algorithm>
#include <fstream>
#include <istream>
#include <iterator>
#include <system_error>

#include "fe/utf8.h"

namespace fe {

Src::Src(std::filesystem::path path, std::string buf)
    : path_(std::move(path))
    , buf_(std::move(buf)) {
    if (buf_.starts_with(utf8::Bom)) bom_ = (uint32_t)utf8::Bom.size();
    rows_.emplace_back(0);
    for (uint32_t i = 0, e = (uint32_t)buf_.size(); i != e; ++i)
        if (buf_[i] == '\n') rows_.emplace_back(i + 1);
}

uint32_t Src::num_rows() const { return (uint32_t)rows_.size() - phantom_(); }

std::pair<uint32_t, uint32_t> Src::rowcol(Pos pos) const {
    if (!contains(pos)) return {0, 0};
    auto row = (uint32_t)(std::ranges::upper_bound(rows_, pos.off) - rows_.begin());

    // @p pos is at the very end of a file that ends with a terminator. That is no row of its own
    // but one past the end of the last real one - which is where an `<end of file>` token points.
    if (row > num_rows()) return {num_rows(), (uint32_t)utf8::num_code_points(line(num_rows())) + 1};

    auto begin = row == 1 ? std::min(bom_, pos.off) : rows_[row - 1];
    return {row, (uint32_t)utf8::num_code_points(sub(begin, pos.off)) + 1};
}

std::string_view Src::line(uint32_t row) const {
    if (row == 0 || row > num_rows()) return {};
    auto begin = row == 1 ? bom_ : rows_[row - 1];
    auto end   = row == rows_.size() ? (uint32_t)buf_.size() : rows_[row] - 1;
    if (end > begin && buf_[end - 1] == '\r') --end;
    return sub(begin, end);
}

Pos Src::prev(Pos pos) const {
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

std::pair<const Src*, bool> SrcMap::add(std::filesystem::path path, std::string buf) {
    auto k          = key(path);
    auto [i, fresh] = path2src_.try_emplace(std::move(k), std::move(path), std::move(buf));
    return {&i->second, fresh};
}

std::pair<const Src*, bool> SrcMap::add(std::filesystem::path path) {
    auto k = key(path);
    if (auto i = path2src_.find(k); i != path2src_.end()) return {&i->second, false};
    auto ifs = std::ifstream(path, std::ios::binary);
    if (!ifs) return {nullptr, false};
    auto [i, fresh] = path2src_.try_emplace(std::move(k), std::move(path), slurp(ifs));
    return {&i->second, fresh};
}

std::string SrcMap::slurp(std::istream& is) {
    return is ? std::string(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>()) : std::string();
}

const Src* SrcMap::lookup(const std::filesystem::path& path) const {
    auto i = path2src_.find(key(path));
    return i == path2src_.end() ? nullptr : &i->second;
}

std::filesystem::path SrcMap::key(const std::filesystem::path& path) {
    std::error_code ec;
    // Absolute first: weakly_canonical only resolves the prefix of `path` that exists on disk,
    // and whether `foo` has such a prefix at all depends on it being spelled `./foo` or not.
    auto abs = std::filesystem::absolute(path, ec);
    if (ec) return path.lexically_normal();
    auto res = std::filesystem::weakly_canonical(abs, ec);
    return ec ? abs.lexically_normal() : res;
}

} // namespace fe
