#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace fe {

static_assert(sizeof(size_t) == 4 || sizeof(size_t) == 8, "unsupported sizeof(size_t)");

/// @name Bit Mixers
/// These finalizers scramble a single word.
/// They are bijective, i.e., they don't introduce any collisions on their own - they merely spread the input bits.
///@{

/// [MurmurHash3](https://en.wikipedia.org/wiki/MurmurHash)'s 32-bit finalizer `fmix32`.
constexpr uint32_t murmur3(uint32_t h) noexcept {
    h ^= h >> 16;
    h *= UINT32_C(0x85ebca6b);
    h ^= h >> 13;
    h *= UINT32_C(0xc2b2ae35);
    h ^= h >> 16;
    return h;
}

/// [SplitMix64](https://prng.di.unimi.it/splitmix64.c)'s 64-bit finalizer.
constexpr uint64_t splitmix64(uint64_t h) noexcept {
    h ^= h >> 30;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 27;
    h *= UINT64_C(0x94d049bb133111eb);
    h ^= h >> 31;
    return h;
}

/// Mixes @p h with murmur3 or splitmix64 - whichever matches `sizeof(size_t)`.
constexpr size_t hash(size_t h) noexcept {
    if constexpr (sizeof(size_t) == 4)
        return size_t(murmur3(uint32_t(h)));
    else
        return size_t(splitmix64(uint64_t(h)));
}
///@}

/// @name FNV-1 Hash
/// See [Wikipedia](https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV-1_hash).
/// Use hash_begin to seed a hash chain and hash_combine to fold in one value after another:
/// ```
/// auto h = fe::hash_begin(x);
/// for (auto elem : elems) h = fe::hash_combine(h, elem);
/// ```
/// @note These hashes are *not* stable:
/// they differ between 32- and 64-bit builds and may change between fe releases.
/// Never serialize them and never rely on the iteration order they induce.
///@{

// clang-format off
/// FNV-1 [magic numbers](http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-var) for `sizeof(size_t)`.
inline constexpr size_t fnv1_offset = sizeof(size_t) == 4 ? size_t(UINT32_C(2166136261)) : size_t(UINT64_C(14695981039346656037));
inline constexpr size_t fnv1_prime  = sizeof(size_t) == 4 ? size_t(UINT32_C(  16777619)) : size_t(UINT64_C(       1099511628211));
// clang-format on

/// Seeds a hash chain with the FNV-1 offset basis.
constexpr size_t hash_begin() noexcept { return fnv1_offset; }

/// Mixes @p v into @p seed word-wise, reusing the FNV-1 prime as multiplier.
template<std::integral T>
constexpr size_t hash_combine(size_t seed, T v) noexcept {
    return hash(seed ^ (size_t(v) * fnv1_prime));
}

/// Shorthand for `hash_combine(hash_begin(), v)`.
template<std::integral T>
constexpr size_t hash_begin(T v) noexcept {
    return hash_combine(hash_begin(), v);
}
///@}

} // namespace fe
