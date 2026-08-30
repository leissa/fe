#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

namespace fe {

/// @name Utility Functions
///@{

/// A bitcast from @p src of type @p S to @p D, supporting different sizes.
/// Keeps the *low-order* bytes on either endianness:
/// a wider @p D zero-fills the rest, a narrower one truncates.
template<class D, class S>
constexpr D bitcast_resize(const S& src) noexcept
    requires(std::is_trivially_copyable_v<S> && std::is_trivially_copyable_v<D>) {
    if constexpr (sizeof(D) == sizeof(S)) {
        return std::bit_cast<D>(src);
    } else {
        constexpr auto n  = std::min(sizeof(D), sizeof(S));
        constexpr auto be = std::endian::native == std::endian::big;
        auto s            = std::bit_cast<std::array<std::byte, sizeof(S)>>(src);
        auto d            = std::array<std::byte, sizeof(D)>{};
        for (size_t i = 0; i != n; ++i)
            d[be ? sizeof(D) - n + i : i] = s[be ? sizeof(S) - n + i : i];
        return std::bit_cast<D>(d);
    }
}

/// Rounds @p offset up to the next multiple of @p align.
[[nodiscard]] constexpr std::uint64_t pad(std::uint64_t offset, std::uint64_t align) noexcept {
    assert(align != 0);

    auto mod = offset % align;
    if (mod) offset += align - mod;
    return offset;
}

[[nodiscard]] constexpr bool is_aligned(std::uint64_t offset, std::uint64_t align) noexcept {
    assert(align != 0);
    return offset % align == 0;
}
///@}

/// @name Algorithms
///@{
template<std::random_access_iterator I, class T, class L = std::less<>>
[[nodiscard]] constexpr I binary_find(I begin, I end, const T& val, L lt = {}) noexcept {
    I i;
    if (std::distance(begin, end) < 16)
        for (i = begin; i != end && lt(*i, val); ++i) {}
    else
        i = std::lower_bound(begin, end, val, lt);
    return (i != end && !lt(val, *i)) ? i : end;
}

template<std::ranges::random_access_range R, class T, class L = std::less<>>
[[nodiscard]] constexpr auto binary_find(R&& r, const T& val, L lt = {}) noexcept requires std::ranges::common_range<R>
{
    return binary_find(std::ranges::begin(r), std::ranges::end(r), val, lt);
}

/// Like `std::string::substr`, but works on `std::string_view` and clamps @p i instead of throwing.
[[nodiscard]] constexpr std::string_view
subview(std::string_view s, size_t i, size_t n = std::string_view::npos) noexcept {
    return s.substr(std::min(i, s.size()), n);
}

/// Replaces all occurrences of @p what with @p repl.
inline void find_and_replace(std::string& str, std::string_view what, std::string_view repl) {
    assert(!what.empty() && "would never terminate");

    for (size_t pos = str.find(what); pos != std::string::npos; pos = str.find(what, pos + repl.size()))
        str.replace(pos, what.size(), repl);
}
///@}

} // namespace fe
