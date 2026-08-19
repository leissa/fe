#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <fe/assert.h>

/// Minimal stand-in for the parts of the C++26 `<simd>` header that fe needs.
/// Built on the GCC/Clang vector extensions instead of intrinsics, so the same source scales with `-march`:
/// scalar by default, AVX2 with `x86-64-v3`, AVX-512 with `x86-64-v4`, NEON on ARM.
/// MSVC has no vector extensions and falls back to the scalar loop.
/// simd::vec/simd::mask correspond to `std::simd`/`std::simd_mask`; the reductions already carry their C++26
/// names, so switching over later is mostly a matter of deleting the two aliases.
namespace fe::simd {

/// Number of elements scanned per block.
/// Buffers passed to simd::find_first must be readable up to a multiple of this.
static constexpr size_t Block = 4;

/// Rounds @p n up to the next multiple of simd::Block.
constexpr size_t pad(size_t n) noexcept { return (n + Block - 1) / Block * Block; }

#if defined(__GNUC__) || defined(__clang__)
using vec [[gnu::vector_size(Block * sizeof(uintptr_t))]] = uintptr_t;                ///< @sa `std::simd`.
using mask                                                = decltype(vec() == vec()); ///< @sa `std::simd_mask`.

/// Is any lane of @p k set? @sa `std::any_of`.
/// Reducing before extracting matters: pulling out every lane would be slower than the scalar loop we replace.
/// GCC has no `__builtin_reduce_or`, but both GCC and Clang fold this into a single `ptest`.
[[nodiscard]] constexpr bool any_of(const mask& k) noexcept {
    auto any = k[0];
    for (size_t i = 1; i != Block; ++i)
        any |= k[i];
    return any != 0;
}

/// Index of the lowest set lane of @p k; @sa `std::reduce_min_index`.
/// @pre `any_of(k)`.
[[nodiscard]] constexpr size_t reduce_min_index(const mask& k) noexcept {
    for (size_t i = 0; i != Block; ++i)
        if (k[i]) return i;
    fe::unreachable();
}
#endif

/// Index of the first `elems[i] == needle` within `[0, n)`, or @p n, if there is none.
/// @warning Scans in simd::Block-sized blocks, so `elems` must be readable up to `simd::pad(n)` and the padding
/// must not contain @p needle.
/// Zero-filling the padding and passing a non-null @p needle guarantees both.
template<class T>
[[nodiscard]] inline size_t find_first(T* const* elems, size_t n, T* needle) noexcept {
    static_assert(sizeof(T*) == sizeof(uintptr_t));
    assert(needle && "a null needle would match the zero padding");

#if defined(__GNUC__) || defined(__clang__)
    auto pattern = vec() + reinterpret_cast<uintptr_t>(needle); // broadcast into all lanes
    for (size_t i = 0; i < n; i += Block) {
        auto chunk = vec();
        std::memcpy(&chunk, elems + i, sizeof(chunk)); // may reach into the padding
        if (auto k = chunk == pattern; any_of(k)) {
            auto res = i + reduce_min_index(k);
            return res < n ? res : n; // only reachable if the caller left the needle in the padding
        }
    }
#else
    for (size_t i = 0; i != n; ++i)
        if (elems[i] == needle) return i;
#endif

    return n;
}

} // namespace fe::simd
