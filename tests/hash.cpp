#include <cstdint>

#include <unordered_set>

#include <doctest/doctest.h>
#include <fe/hash.h>

namespace {

// The mixers are bijective: check that they don't collide on a large chunk of their domain.
template<class T, class F>
bool bijective(F f, T e) {
    std::unordered_set<T> seen;
    for (T i = 0; i != e; ++i)
        if (!seen.emplace(f(i)).second) return false;
    return true;
}

} // namespace

TEST_CASE("hash") {
    SUBCASE("murmur3") {
        // Reference values of MurmurHash3's fmix32.
        CHECK(fe::murmur3(0) == UINT32_C(0)); // 0 is a fixed point of any xor-shift-multiply finalizer
        CHECK(fe::murmur3(1) == UINT32_C(1364076727));
        CHECK(fe::murmur3(UINT32_C(0xffffffff)) == UINT32_C(2180083513));
        CHECK(bijective<uint32_t>(fe::murmur3, 100000));
    }

    SUBCASE("splitmix64") {
        // Reference values of SplitMix64's finalizer.
        CHECK(fe::splitmix64(0) == UINT64_C(0)); // see above
        CHECK(fe::splitmix64(1) == UINT64_C(6238072747940578789));
        CHECK(bijective<uint64_t>(fe::splitmix64, 100000));
    }

    SUBCASE("hash dispatches on sizeof(size_t)") {
        if constexpr (sizeof(size_t) == 4)
            CHECK(fe::hash(23) == size_t(fe::murmur3(23)));
        else
            CHECK(fe::hash(23) == size_t(fe::splitmix64(23)));

        CHECK(bijective<size_t>(fe::hash, 100000));
    }

    SUBCASE("hash_begin/hash_combine") {
        static_assert(fe::hash_begin() == fe::fnv1_offset);
        static_assert(fe::hash_begin(23) == fe::hash_combine(fe::hash_begin(), 23));

        // A hash chain is order-sensitive ...
        CHECK(fe::hash_combine(fe::hash_begin(1), 2) != fe::hash_combine(fe::hash_begin(2), 1));
        // ... and sensitive to length: appending 0 must not be a no-op.
        CHECK(fe::hash_combine(fe::hash_begin(1), 0) != fe::hash_begin(1));

        // Typical usage: hashing small consecutive ids must not clump.
        std::unordered_set<size_t> seen;
        for (size_t i = 0; i != 1000; ++i)
            for (size_t j = 0; j != 100; ++j)
                seen.emplace(fe::hash_combine(fe::hash_begin(i), j));
        CHECK(seen.size() == 1000 * 100);
    }

    SUBCASE("hash_combine accepts any integral type") {
        static_assert(fe::hash_begin(int8_t(-1)) == fe::hash_begin(size_t(-1)));
        static_assert(fe::hash_begin(true) == fe::hash_begin(1));
        static_assert(fe::hash_begin('a') == fe::hash_begin(97));
    }
}
