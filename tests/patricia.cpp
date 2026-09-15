#include <cstdint>

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>
#include <fe/patricia.h>

namespace {

/// Deliberately tiny, so that the Leaf/Arr/Br boundaries are crossed all the time.
static constexpr size_t N = 4;

using P     = fe::Patricia<uint32_t, uint64_t, N>;
using M     = P::Map;
using Entry = P::Entry;
using Ref   = std::map<uint64_t, uint32_t>;

Ref ref_of(M map) {
    auto res = Ref();
    for (const auto& e : map)
        res.emplace(e.key, e.val);
    return res;
}

M make(P& p, const Ref& ref) {
    auto v = std::vector<Entry>();
    for (const auto& [key, val] : ref)
        v.emplace_back(Entry{key, val});
    return p.create(v);
}

/// Everything the public API can observe about a Map - contents, order, and above all *canonicity*:
/// building the same bindings from scratch must land on the very same Map, which is exactly what breaks
/// if a node fails to flatten back into an Arr, an Arr outgrows N, or a branch gets the wrong prefix.
void check(P& p, M map, const Ref& want) {
    REQUIRE(map.size() == want.size());
    REQUIRE(map.empty() == want.empty());

    auto got = std::vector<std::pair<uint64_t, uint32_t>>();
    for (const auto& e : map)
        got.emplace_back(e.key, e.val);

    REQUIRE(got.size() == want.size());
    REQUIRE(std::ranges::is_sorted(got, {}, &std::pair<uint64_t, uint32_t>::first));
    REQUIRE(std::equal(got.begin(), got.end(), want.begin(), want.end()));

    for (const auto& [key, val] : want) {
        auto e = map.find(key);
        REQUIRE(e != nullptr);
        REQUIRE(e->val == val);
    }

    REQUIRE(make(p, want) == map);

    if (want.empty()) {
        REQUIRE(map.min() == nullptr);
        REQUIRE(map.max() == nullptr);
    } else {
        REQUIRE(map.min()->key == want.begin()->first);
        REQUIRE(map.max()->key == want.rbegin()->first);
    }
}

Ref spread(size_t n, uint64_t offset = 0, uint64_t stride = 2) {
    auto res = Ref();
    for (uint64_t i = 0; i != n; ++i)
        res.emplace(i * stride + offset, uint32_t(i + 1));
    return res;
}

const auto sizes = {size_t(0), size_t(1), size_t(2), N - 1, N, N + 1, 2 * N, 3 * N, size_t(50)};

} // namespace

TEST_CASE("Patricia") {
    auto p = P();

    SUBCASE("flavours") {
        for (auto n : sizes) {
            INFO("n = ", n);
            auto want = spread(n);
            check(p, make(p, want), want);
        }
    }

    SUBCASE("unsigned key order") {
        // A signed comparison anywhere would sort these the other way round.
        auto want = Ref{
            {                0, 1},
            {                1, 2},
            {uint64_t(1) << 62, 3},
            {uint64_t(1) << 63, 4},
            {     uint64_t(-2), 5},
            {     uint64_t(-1), 6}
        };
        auto map = make(p, want);
        check(p, map, want);
        REQUIRE(map.min()->key == 0);
        REQUIRE(map.max()->key == uint64_t(-1));
    }

    SUBCASE("create dedups, keeps the last binding, and is order independent") {
        for (auto n : sizes) {
            INFO("n = ", n);
            auto fwd = std::vector<Entry>();
            for (uint64_t i = 0; i != n; ++i)
                fwd.emplace_back(Entry{i * 3 + 5, uint32_t(i)});

            auto rev = std::vector<Entry>(fwd.rbegin(), fwd.rend());
            REQUIRE(p.create(fwd) == p.create(rev));

            if (n != 0) { // a stale binding in front must lose against the real one behind it
                auto dup = std::vector<Entry>{
                    Entry{fwd.front().key, 0xdead}
                };
                dup.insert(dup.end(), fwd.begin(), fwd.end());
                REQUIRE(p.create(dup) == p.create(fwd));
            }
        }
    }

    SUBCASE("canonical across build paths") {
        // create, repeated insert, and merging two halves must all agree - they exercise different code.
        for (auto n : sizes) {
            INFO("n = ", n);
            auto want = spread(n, 7, 5);

            auto by_insert = M();
            for (const auto& [key, val] : want)
                by_insert = p.insert(by_insert, key, val);

            auto lo = Ref(), hi = Ref();
            for (const auto& [key, val] : want)
                (lo.size() <= hi.size() ? lo : hi).emplace(key, val);
            auto by_merge = p.merge(make(p, lo), make(p, hi));

            auto by_erase = make(p, want);
            by_erase      = p.insert(by_erase, 1234567, 42);
            by_erase      = p.erase(by_erase, 1234567);

            REQUIRE(by_insert == make(p, want));
            REQUIRE(by_merge == make(p, want));
            REQUIRE(by_erase == make(p, want));
            check(p, by_insert, want);
        }
    }

    SUBCASE("insert") {
        auto want = Ref();
        auto map  = M();

        for (uint32_t i = 0; i != 3 * N + 20; ++i) {
            INFO("i = ", i);
            auto key  = uint64_t(i * 11 + 3) % 97;
            map       = p.insert(map, key, i);
            want[key] = i;
            check(p, map, want);

            CHECK_MESSAGE(p.insert(map, key, i) == map, "inserting the same binding again must be a no-op");
            CHECK_MESSAGE(p.insert(map, key, i, P::Fst()) == map, "and so must be a combiner keeping the old one");
            CHECK(p.insert(map, key, i + 1).find(key)->val == i + 1); // the new value wins by default
        }
    }

    SUBCASE("erase") {
        for (auto n : sizes) {
            INFO("n = ", n);
            auto want = spread(n);
            auto map  = make(p, want);

            CHECK_MESSAGE(p.erase(map, 999999) == map, "erasing an absent key must be a no-op");

            auto keys = std::vector<uint64_t>();
            for (const auto& [key, _] : want)
                keys.emplace_back(key);

            for (auto key : keys) {
                map = p.erase(map, key);
                want.erase(key);
                check(p, map, want);
            }
            CHECK(map.empty());
        }
    }

    SUBCASE("merge matrix") {
        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                for (uint64_t offset : {uint64_t(0), uint64_t(1), uint64_t(1000)}) { // overlap, interleave, disjoint
                    INFO(n1, " u ", n2, " @", offset);
                    auto w1 = spread(n1);
                    auto w2 = spread(n2, offset);

                    auto m1 = make(p, w1);
                    auto m2 = make(p, w2);

                    auto want = w2; // merge keeps the left value, so w1 overwrites w2
                    for (const auto& [key, val] : w1)
                        want[key] = val;

                    auto m = p.merge(m1, m2);
                    check(p, m, want);
                    CHECK_MESSAGE(p.merge(m, m1) == m, "absorption");
                    CHECK_MESSAGE(p.merge(m1, m1) == m1, "idempotence");
                    CHECK_MESSAGE(p.merge(m2, m1, P::Snd()) == m, "flipping operands and combiner is the same");
                }
            }
        }
    }

    SUBCASE("intersect matrix") {
        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                for (uint64_t offset : {uint64_t(0), uint64_t(1), uint64_t(1000)}) {
                    INFO(n1, " ^ ", n2, " @", offset);
                    auto w1 = spread(n1);
                    auto w2 = spread(n2, offset);

                    auto want = Ref();
                    for (const auto& [key, val] : w1)
                        if (w2.contains(key)) want[key] = val;

                    auto m1 = make(p, w1);
                    auto m2 = make(p, w2);
                    auto m  = p.intersect(m1, m2);
                    check(p, m, want);
                    CHECK_MESSAGE(p.intersect(m2, m1, P::Snd()) == m, "flipping operands and combiner is the same");
                    CHECK(m1.has_intersection(m2) == !want.empty());
                    CHECK(m2.has_intersection(m1) == !want.empty());
                }
            }
        }
    }

    SUBCASE("diff matrix") {
        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                for (uint64_t offset : {uint64_t(0), uint64_t(1), uint64_t(1000)}) {
                    INFO(n1, " \\ ", n2, " @", offset);
                    auto w1 = spread(n1);
                    auto w2 = spread(n2, offset);

                    auto want = Ref();
                    for (const auto& [key, val] : w1)
                        if (!w2.contains(key)) want[key] = val;

                    auto m1 = make(p, w1);
                    auto m2 = make(p, w2);
                    check(p, p.diff(m1, m2), want);
                    CHECK(p.diff(m1, m1).empty());
                    CHECK(p.diff(m1, M()) == m1);
                }
            }
        }
    }

    SUBCASE("subset_of") {
        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                INFO(n1, " <= ", n2);
                auto w1 = spread(n1);
                auto w2 = spread(n2);

                auto want = std::ranges::all_of(w1, [&](const auto& kv) { return w2.contains(kv.first); });
                CHECK(make(p, w1).subset_of(make(p, w2)) == want);
            }
        }
    }

    SUBCASE("combiner argument order and dropping") {
        auto w1 = spread(3 * N);
        auto w2 = spread(3 * N, 0, 4); // every other key of w1

        auto m1  = make(p, w1);
        auto m2  = make(p, w2);
        auto mix = [](uint64_t, uint32_t v1, uint32_t v2) { return v1 * 1000 + v2; };

        auto want = w2;
        for (const auto& [key, val] : w1)
            want[key] = w2.contains(key) ? val * 1000 + w2[key] : val;
        check(p, p.merge(m1, m2, mix), want);

        // A combiner may drop an entry - which must collapse the branches above it all the way back down.
        auto drop = [](uint64_t key, uint32_t v1, uint32_t) {
            return key % 3 == 0 ? std::optional<uint32_t>() : std::optional<uint32_t>(v1);
        };
        auto dropped = Ref();
        for (const auto& [key, val] : w1)
            if (!w2.contains(key) || key % 3 != 0) dropped[key] = val;
        for (const auto& [key, val] : w2)
            if (!w1.contains(key)) dropped[key] = val;
        check(p, p.merge(m1, m2, drop), dropped);

        auto kept = Ref();
        for (const auto& [key, val] : w1)
            if (w2.contains(key) && key % 3 != 0) kept[key] = val;
        check(p, p.intersect(m1, m2, drop), kept);
    }

    SUBCASE("output and for_each") {
        auto want = spread(3 * N);
        auto map  = make(p, want);

        auto seen = Ref();
        map.for_each([&](const Entry& e) { seen.emplace(e.key, e.val); });
        CHECK(seen == want);

        auto os = std::ostringstream();
        os << map;
        CHECK(os.str().starts_with("{0: 1, 2: 2, 4: 3,"));
        CHECK(os.str().ends_with("}"));
        CHECK(&map.stream(os) == &os);

        auto dot = std::ostringstream();
        map.dot(dot);
        CHECK(dot.str().starts_with("digraph {"));
        CHECK(dot.str().find("->") != std::string::npos); // 3 * N entries, so there *are* branches

        auto empty = std::ostringstream();
        empty << M();
        CHECK(empty.str() == "{}");
    }

    /// Differential test: drive Patricia and std::map through the same random operations.
    SUBCASE("random vs std::map") {
        auto rng  = std::mt19937(42);
        auto keys = std::vector<uint64_t>{0, 1, 2, 3, uint64_t(1) << 63, uint64_t(-1), uint64_t(-2)};
        for (uint64_t i = 0; i != 121; ++i)
            keys.emplace_back(i * 7 % 211);

        auto ref = std::vector<Ref>{{}};
        auto got = std::vector<M>{{}};

        for (int step = 0; step != 4000; ++step) {
            INFO("step = ", step);
            auto i  = rng() % got.size();
            auto j  = rng() % got.size();
            auto op = rng() % 6;
            INFO("op = ", op);

            auto key = keys[rng() % keys.size()];
            auto val = uint32_t(rng() % 1000);
            auto r   = ref[i];

            if (op == 0) {
                got.emplace_back(p.insert(got[i], key, val));
                r[key] = val;
            } else if (op == 1) {
                got.emplace_back(p.erase(got[i], key));
                r.erase(key);
            } else if (op == 2) {
                got.emplace_back(p.merge(got[i], got[j]));
                for (const auto& [k, v] : ref[j])
                    r.emplace(k, v); // merge keeps the left value
            } else if (op == 3) {
                got.emplace_back(p.intersect(got[i], got[j]));
                std::erase_if(r, [&](const auto& kv) { return !ref[j].contains(kv.first); });
            } else if (op == 4) {
                got.emplace_back(p.diff(got[i], got[j]));
                std::erase_if(r, [&](const auto& kv) { return ref[j].contains(kv.first); });
            } else { // queries only
                REQUIRE(got[i].contains(key) == ref[i].contains(key));

                auto meets = std::ranges::any_of(ref[i], [&](const auto& kv) { return ref[j].contains(kv.first); });
                REQUIRE(got[i].has_intersection(got[j]) == meets);

                auto sub = std::ranges::all_of(ref[i], [&](const auto& kv) { return ref[j].contains(kv.first); });
                REQUIRE(got[i].subset_of(got[j]) == sub);
                continue;
            }

            ref.emplace_back(std::move(r));
            check(p, got.back(), ref.back());

            if (got.size() > 48) { // keep the working set bounded
                got.erase(got.begin() + 1, got.begin() + 24);
                ref.erase(ref.begin() + 1, ref.begin() + 24);
            }
        }
    }
}

TEST_CASE("PatriciaSet") {
    using S = fe::PatriciaSet;
    static_assert(sizeof(S::Entry) == sizeof(uint64_t), "Unit must not cost anything");

    auto p = S();
    auto s = S::Set();

    auto want = std::set<uint64_t>();
    for (uint64_t i = 0; i != 100; ++i) {
        auto key = i * 37 % 211;
        s        = p.insert(s, key);
        want.emplace(key);

        auto got = std::set<uint64_t>();
        for (const auto& e : s)
            got.emplace(e.key);
        REQUIRE(got == want);
        REQUIRE(s.size() == want.size());
        REQUIRE(p.create(want) == s);
    }

    auto evens = p.create(std::vector<uint64_t>{0, 2, 4, 6, 8, 10, 12, 14});
    auto odds  = p.create(std::vector<uint64_t>{1, 3, 5, 7, 9, 11, 13, 15});
    CHECK(!evens.has_intersection(odds));
    CHECK(p.intersect(evens, odds).empty());
    CHECK(p.merge(evens, odds).size() == 16);
    CHECK(evens.subset_of(p.merge(evens, odds)));
    CHECK(p.diff(p.merge(evens, odds), odds) == evens);
}
