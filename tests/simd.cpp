#include <cstdint>

#include <vector>

#include <doctest/doctest.h>
#include <fe/simd.h>

using namespace fe;

namespace {

/// A haystack of @p n distinct non-null pointers, padded to simd::pad(n) and zero-filled as find_first requires.
std::vector<const void*> haystack(size_t n) {
    auto v = std::vector<const void*>(simd::pad(n), nullptr);
    for (size_t i = 0; i != n; ++i)
        v[i] = reinterpret_cast<const void*>(uintptr_t((i + 1) * 8));
    return v;
}

} // namespace

TEST_CASE("simd::pad") {
    CHECK(simd::pad(0) == 0);
    CHECK(simd::pad(1) == simd::Block);
    CHECK(simd::pad(simd::Block) == simd::Block);
    CHECK(simd::pad(simd::Block + 1) == 2 * simd::Block);

    for (size_t n = 0; n != 64; ++n) {
        CHECK(simd::pad(n) >= n);
        CHECK(simd::pad(n) % simd::Block == 0);
        CHECK(simd::pad(n) - n < simd::Block);
    }
}

TEST_CASE("simd::find_first") {
    SUBCASE("empty") {
        auto v = haystack(0);
        CHECK(simd::find_first(v.data(), 0, reinterpret_cast<const void*>(uintptr_t(8))) == 0);
    }

    SUBCASE("hit at every position") {
        for (size_t n = 1; n != 40; ++n) {
            auto v = haystack(n);
            for (size_t i = 0; i != n; ++i)
                CHECK(simd::find_first(v.data(), n, v[i]) == i);
        }
    }

    SUBCASE("miss") {
        auto absent = reinterpret_cast<const void*>(uintptr_t(0xdead8));
        for (size_t n = 0; n != 40; ++n) {
            auto v = haystack(n);
            CHECK(simd::find_first(v.data(), n, absent) == n);
        }
    }

    SUBCASE("a needle living only in the padding is not found") {
        // The contract says the padding must not contain the needle; if it does anyway, find_first must still
        // never report an index outside [0, n).
        for (size_t n = 1; n != 40; ++n) {
            if (simd::pad(n) == n) continue;
            auto v   = haystack(n);
            auto pad = reinterpret_cast<const void*>(uintptr_t(0xbeef8));
            for (auto i = n; i != simd::pad(n); ++i)
                v[i] = pad;
            CHECK(simd::find_first(v.data(), n, pad) == n);
        }
    }

    SUBCASE("duplicates yield the first index") {
        auto v = haystack(8);
        v[5]   = v[2];
        CHECK(simd::find_first(v.data(), 8, v[2]) == 2);
    }
}
