#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

#include <functional>
#include <string>
#include <string_view>

#include <ankerl/unordered_dense.h>

namespace fe {

/// Folds @p v into @p seed like `ankerl::unordered_dense` hashes a tuple: one 128-bit multiply, halves xored.
/// Each step avalanches, so a chain needs no finalizer and suits a hasher that declares `is_avalanching`.
/// Seed a chain with `0`:
/// ```
/// size_t h = 0;
/// for (auto elem : elems) h = fe::hash_combine(h, elem);
/// ```
/// @note These hashes are *not* stable:
/// they differ between 32- and 64-bit builds and may change between fe releases.
/// Never serialize them and never rely on the iteration order they induce.
template<std::integral T>
constexpr size_t hash_combine(size_t seed, T v) noexcept {
    constexpr auto Secret = UINT64_C(0x9ddfea08eb382d69);
    auto a                = uint64_t(seed) + uint64_t(v);

    if !consteval {
        return size_t(ankerl::unordered_dense::detail::hash_impl::mix(a, Secret));
    } else {
        uint64_t ha = a >> 32, la = uint32_t(a);
        uint64_t hb = Secret >> 32, lb = uint32_t(Secret);
        uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb;
        uint64_t t  = rl + (rm0 << 32);
        uint64_t c  = t < rl;
        uint64_t lo = t + (rm1 << 32);
        c += lo < t;
        uint64_t hi = rh + (rm0 >> 32) + (rm1 >> 32) + c;
        return size_t(lo ^ hi);
    }
}

/// Hashes the characters of a string; transparent, so `std::string` keys may be looked up by `std::string_view`.
struct StrHash {
    using is_transparent = void;
    using is_avalanching = void;

    size_t operator()(std::string_view sv) const noexcept {
        return ankerl::unordered_dense::hash<std::string_view>()(sv);
    }
};

/// @name StrMap/StrSet
/// Keyed by `std::string` but also looked up by `std::string_view` or `const char*` without building a `std::string`.
///@{
template<class V>
using StrMap = ankerl::unordered_dense::map<std::string, V, StrHash, std::equal_to<>>;
using StrSet = ankerl::unordered_dense::set<std::string, StrHash, std::equal_to<>>;
///@}

} // namespace fe
