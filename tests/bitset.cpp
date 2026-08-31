#include <cstddef>

#include <format>
#include <ranges>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

#include <doctest/doctest.h>
#include <fe/bitset.h>
#include <fe/format.h>

using fe::Bitset;

namespace {

std::vector<size_t> elems(const Bitset& bitset) { return {bitset.begin(), bitset.end()}; }

constexpr bool inline_ops() {
    auto b = Bitset{1, 3, 63};
    b.flip(3);
    b.set(2);
    b.clear(1);
    if (b.count() != 2 || !b.test(63) || b.test(3)) return false;
    if (b.next(0) != 2 || b.next(3) != 63 || b.next(64) != Bitset::npos) return false;
    if (b.on_heap() || b.capacity() != Bitset::Inline_Bits) return false;

    b[0] = true;
    if (!b[0] || !~b[1]) return false;

    auto c = (b & Bitset{0, 2}) | Bitset{7};
    if (c != Bitset{0, 2, 7} || !Bitset{2}.subset_of(c) || !c.intersects(Bitset{7})) return false;
    if (!(c - c).none() || (c ^ Bitset{0}) != Bitset{2, 7}) return false;

    auto d = c;
    swap(d, b);
    if (d != Bitset{0, 2, 63} || c != Bitset{0, 2, 7}) return false;

    size_t sum = 0;
    for (auto i : c)
        sum += i;
    return sum == 9 && c.hash() == Bitset{0, 2, 7}.hash();
}

static_assert(inline_ops());

} // namespace

TEST_CASE("Bitset") {
    SUBCASE("empty") {
        auto b = Bitset();
        CHECK(!b.on_heap());
        CHECK(b.none());
        CHECK(!b.any());
        CHECK(b.count() == 0);
        CHECK(b.next(0) == Bitset::npos);
        CHECK(b.begin() == b.end());
        CHECK(!b.test(0));
        CHECK(!b.test(1'000'000)); // does not allocate
        CHECK(!b.on_heap());
    }

    SUBCASE("inline") {
        auto b = Bitset{0, 3, 63};
        CHECK(!b.on_heap());
        CHECK(b.capacity() == Bitset::Inline_Bits);
        CHECK(b.count() == 3);
        CHECK(elems(b) == std::vector<size_t>{0, 3, 63});
        CHECK(b.next(1) == 3);
        CHECK(b.next(4) == 63);
        CHECK(b.next(64) == Bitset::npos);

        b.clear(3);
        CHECK(!b.test(3));
        CHECK(b.count() == 2);
    }

    SUBCASE("grow") {
        auto b = Bitset();
        b.set(64);
        CHECK(b.on_heap());
        CHECK(b.capacity() >= 128);
        CHECK(b.count() == 1);
        CHECK(elems(b) == std::vector<size_t>{64});

        b.set(1'000);
        CHECK(elems(b) == std::vector<size_t>{64, 1'000});
        CHECK(b.test(1'000));

        b.clear();
        CHECK(!b.on_heap());
        CHECK(b.none());
    }

    SUBCASE("set/clear/flip") {
        auto b = Bitset();
        for (size_t i = 0; i < 300; i += 7)
            b.set(i);
        for (size_t i = 0; i != 300; ++i)
            CHECK(b.test(i) == (i % 7 == 0));

        b.flip(7);
        CHECK(!b.test(7));
        b.flip(7);
        CHECK(b.test(7));
        b.set(7, false);
        CHECK(!b.test(7));
        b.set(7, true);
        CHECK(b.test(7));

        b[0] = false;
        CHECK(!b[0]);
        b[0].flip();
        CHECK(b[0]);
        CHECK(~b[1]);

        auto c = Bitset();
        c[500] = false; // clearing an unset bit never allocates
        CHECK(!c.on_heap());
        c[500] = true;
        CHECK(c.test(500));
    }

    SUBCASE("iteration") {
        auto b = Bitset{1, 64, 65, 191, 192};
        CHECK(elems(b) == std::vector<size_t>{1, 64, 65, 191, 192});
        CHECK(std::ranges::distance(b) == 5);

        auto i = b.begin();
        auto j = i++;
        CHECK(*j == 1);
        CHECK(*i == 64);
        CHECK(i != j);
    }

    SUBCASE("copy/move/swap") {
        auto big   = Bitset{2, 500};
        auto small = Bitset{2};
        CHECK(big.on_heap());
        CHECK(!small.on_heap());

        auto copy = big;
        CHECK(copy == big);
        CHECK(copy.on_heap());
        copy.set(3);
        CHECK(!big.test(3));

        auto move = std::move(copy);
        CHECK(move.test(3));
        CHECK(move.test(500));

        swap(big, small);
        CHECK(elems(big) == std::vector<size_t>{2});
        CHECK(elems(small) == std::vector<size_t>{2, 500});

        small = big;
        CHECK(elems(small) == std::vector<size_t>{2});
        small = Bitset{7};
        CHECK(elems(small) == std::vector<size_t>{7});
        auto& ref = small;
        small     = ref;
        CHECK(elems(small) == std::vector<size_t>{7});
    }

    SUBCASE("set operations") {
        auto a = Bitset{1, 3, 200};
        auto b = Bitset{3, 4};

        CHECK(elems(a | b) == std::vector<size_t>{1, 3, 4, 200});
        CHECK(elems(a & b) == std::vector<size_t>{3});
        CHECK(elems(a ^ b) == std::vector<size_t>{1, 4, 200});
        CHECK(elems(a - b) == std::vector<size_t>{1, 200});
        CHECK(elems(b - a) == std::vector<size_t>{4});

        CHECK(elems(a) == std::vector<size_t>{1, 3, 200});
        CHECK(elems(b) == std::vector<size_t>{3, 4});

        CHECK(!b.on_heap());
        b |= a;
        CHECK(b.on_heap());
        CHECK(elems(b) == std::vector<size_t>{1, 3, 4, 200});

        b &= a;
        CHECK(elems(b) == std::vector<size_t>{1, 3, 200});
        CHECK(b == a);

        auto c = a;
        c ^= c;
        CHECK(c.none());
        c |= c;
        CHECK(c.none());
    }

    SUBCASE("comparisons") {
        auto a = Bitset{5};
        auto b = Bitset{5, 500};
        b.clear(500);
        CHECK(b.on_heap());
        CHECK(!a.on_heap());
        CHECK(a == b); // capacity does not matter
        CHECK(a.subset_of(b));
        CHECK(b.subset_of(a));

        b.set(500);
        CHECK(a != b);
        CHECK(a.subset_of(b));
        CHECK(!b.subset_of(a));
        CHECK(a.intersects(b));
        CHECK(b.intersects(a));
        CHECK(!Bitset{6}.intersects(a));
        CHECK(Bitset().subset_of(a));
        CHECK(!a.subset_of(Bitset()));
    }

    SUBCASE("hash") {
        auto a = Bitset{5};
        auto b = Bitset{5, 500};
        b.clear(500);
        CHECK(a.hash() == b.hash()); // trailing zero words do not contribute

        auto set = std::unordered_set<Bitset>();
        CHECK(set.emplace(a).second);
        CHECK(!set.emplace(b).second);
        CHECK(set.emplace(Bitset{5, 500}).second);
        CHECK(set.size() == 2);
    }

    SUBCASE("constexpr") { CHECK(inline_ops()); }

    SUBCASE("output") {
        auto b  = Bitset{1, 3, 100};
        auto os = std::ostringstream();
        os << b;
        CHECK(os.str() == "{1, 3, 100}");
        CHECK(std::format("{}", b) == "{1, 3, 100}");
        CHECK(std::format("{}", Bitset()) == "{}");
    }
}
