#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>

#include <algorithm>
#include <bit>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

namespace fe {

/// @name Utility Functions
///@{

/// A bitcast from @p src of type @p S to @p D, supporting different sizes.
template<class D, class S>
constexpr D bitcast_resize(const S& src) noexcept
    requires(std::is_trivially_copyable_v<S> && std::is_trivially_copyable_v<D>)
{
    if constexpr (sizeof(D) == sizeof(S)) {
        return std::bit_cast<D>(src);
    } else {
        D dst{}; // zero-fill
        constexpr std::size_t n = (sizeof(D) < sizeof(S)) ? sizeof(D) : sizeof(S);
        std::memcpy(&dst, &src, n);
        return dst;
    }
}

constexpr std::uint64_t pad(std::uint64_t offset, std::uint64_t align) noexcept {
    assert(align != 0);

    auto mod = offset % align;
    if (mod) offset += align - mod;
    return offset;
}
///@}

/// @name Algorithms
///@{
template<class I, class T, class L>
constexpr I binary_find(I begin, I end, T val, L lt) noexcept {
    static_assert(std::random_access_iterator<I>);
    I i;
    if (std::distance(begin, end) < 16)
        for (i = begin; i != end && lt(*i, val); ++i) {}
    else
        i = std::lower_bound(begin, end, val, lt);
    return (i != end && !lt(val, *i)) ? i : end;
}

/// Like `std::string::substr`, but works on `std::string_view` instead.
constexpr std::string_view subview(std::string_view s, size_t i, size_t n = std::string_view::npos) noexcept {
    i = std::min(i, s.size());
    return {s.data() + i, std::min(n, s.size() - i)};
}

/// Replaces all occurrences of @p what with @p repl.
inline void find_and_replace(std::string& str, std::string_view what, std::string_view repl) {
    for (size_t pos = str.find(what); pos != std::string::npos; pos = str.find(what, pos + repl.size()))
        str.replace(pos, what.size(), repl);
}
///@}

} // namespace fe
