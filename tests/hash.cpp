#include <cstdint>

#include <string_view>
#include <unordered_set>

#include <doctest/doctest.h>
#include <fe/container.h>
#include <fe/hash.h>

TEST_CASE("hash") {
    SUBCASE("hash_combine") {
        // Constant-evaluated and run-time hashing must agree.
        constexpr auto h = fe::hash_combine(fe::hash_combine(0, 1), 2);
        auto one         = 1;
        CHECK(fe::hash_combine(fe::hash_combine(0, one), 2) == h);
        CHECK(fe::hash_combine(0, UINT64_C(0xffffffffffffffff)) == fe::hash_combine(0, size_t(-1)));

        // A hash chain is order-sensitive ...
        CHECK(fe::hash_combine(fe::hash_combine(0, 1), 2) != fe::hash_combine(fe::hash_combine(0, 2), 1));
        // ... and sensitive to length: appending 0 must not be a no-op.
        CHECK(fe::hash_combine(fe::hash_combine(0, 1), 0) != fe::hash_combine(0, 1));

        // Typical usage: hashing small consecutive ids must not clump.
        std::unordered_set<size_t> seen;
        for (size_t i = 0; i != 1000; ++i)
            for (size_t j = 0; j != 100; ++j)
                seen.emplace(fe::hash_combine(fe::hash_combine(0, i), j));
        CHECK(seen.size() == 1000 * 100);
    }

    SUBCASE("hash_combine accepts any integral type") {
        static_assert(fe::hash_combine(0, int8_t(-1)) == fe::hash_combine(0, size_t(-1)));
        static_assert(fe::hash_combine(0, true) == fe::hash_combine(0, 1));
        static_assert(fe::hash_combine(0, 'a') == fe::hash_combine(0, 97));
    }

    SUBCASE("StrMap/StrSet") {
        auto map = fe::StrMap<int>();
        map.emplace("foo", 23);
        auto sv = std::string_view("foobar").substr(0, 3);
        CHECK(*fe::lookup(map, sv) == 23);
        CHECK(fe::lookup(map, "bar") == nullptr);
        map[std::string_view("bar")] = 42;
        CHECK(fe::assert_lookup(map, "bar") == 42);

        auto set = fe::StrSet{"foo"};
        CHECK(set.contains(sv));
        CHECK(!set.contains("bar"));
    }
}
