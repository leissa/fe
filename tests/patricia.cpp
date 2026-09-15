#include <cstdint>

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>
#include <fe/patricia.h>

namespace {

/// Deliberately tiny, so that the Uniq/Arr/Br boundaries are crossed all the time.
static constexpr size_t N = 4;

/// Minimal element: fe::Patricia only needs an id to key and order by.
/// The `alignas` is the contract - a Set tags the two low bits of its word.
struct alignas(4) Elem {
    uint32_t gid;
};

struct ElemKey {
    static uint32_t key(const Elem* e) noexcept { return e->gid; }
    static std::ostream& stream(std::ostream& os, const Elem* e) { return os << 'e' << e->gid; }
};

using P   = fe::Patricia<Elem, ElemKey, uint32_t, N>;
using S   = P::Set;
using Ref = std::set<uint32_t>;

/// One stable Elem per id - a Set identifies its elements by id, which is exactly what the reference Ref means.
Elem* elem(uint32_t gid) {
    static auto pool = std::map<uint32_t, Elem>();
    return &pool.emplace(gid, Elem{gid}).first->second;
}

S make(P& p, const Ref& ref) {
    auto v = std::vector<Elem*>();
    for (auto gid : ref)
        v.emplace_back(elem(gid));
    return p.create(v);
}

/// Everything the public API can observe about a Set - contents, order, and above all *canonicity*:
/// building the same elements from scratch must land on the very same Set, which is exactly what breaks
/// if a node fails to flatten back into an Arr or a Uniq, an Arr outgrows N, or a branch gets the wrong prefix.
void check(P& p, S s, const Ref& want) {
    REQUIRE(s.size() == want.size());
    REQUIRE(s.empty() == want.empty());
    REQUIRE(bool(s) == !want.empty());

    auto got = std::vector<uint32_t>();
    for (auto d : s)
        got.emplace_back(d->gid);

    REQUIRE(got.size() == want.size());
    REQUIRE(std::ranges::is_sorted(got)); // ascending id, not insertion order
    REQUIRE(std::equal(got.begin(), got.end(), want.begin(), want.end()));

    for (auto gid : want)
        REQUIRE(s.contains(elem(gid)));

    REQUIRE(make(p, want) == s);
    REQUIRE(s.subset_of(make(p, want)));

    if (want.empty()) {
        REQUIRE(s.min() == nullptr);
        REQUIRE(s.max() == nullptr);
    } else {
        REQUIRE(s.min() == elem(*want.begin()));
        REQUIRE(s.max() == elem(*want.rbegin()));
    }
}

Ref spread(size_t n, uint32_t offset = 0, uint32_t stride = 2) {
    auto res = Ref();
    for (uint32_t i = 0; i != n; ++i)
        res.emplace(i * stride + offset);
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

    SUBCASE("a singleton is always inline") {
        auto e = elem(23);
        auto s = S(e);
        check(p, s, Ref{23});
        CHECK(s == p.create({e}));
        CHECK(s == p.insert(S(), e));
        CHECK(s == p.merge(S(), s));
        CHECK(s == p.erase(p.create({e, elem(42)}), elem(42)));
        CHECK(s == p.intersect(make(p, spread(3 * N, 23, 1)), s));
        CHECK(s == p.diff(p.create({e, elem(42)}), p.create({elem(42)})));
        CHECK(S() == p.erase(s, e));
    }

    SUBCASE("unsigned id order") {
        // A signed comparison anywhere would sort these the other way round.
        auto want = Ref{0, 1, uint32_t(1) << 30, uint32_t(1) << 31, uint32_t(-2), uint32_t(-1)};
        auto s    = make(p, want);
        check(p, s, want);
        REQUIRE(s.min() == elem(0));
        REQUIRE(s.max() == elem(uint32_t(-1)));
    }

    SUBCASE("create dedups and is order independent") {
        for (auto n : sizes) {
            INFO("n = ", n);
            auto fwd = std::vector<Elem*>();
            for (uint32_t i = 0; i != n; ++i)
                fwd.emplace_back(elem(i * 3 + 5));

            auto rev = std::vector<Elem*>(fwd.rbegin(), fwd.rend());
            REQUIRE(p.create(fwd) == p.create(rev));

            auto dup = fwd;
            dup.insert(dup.end(), fwd.begin(), fwd.end());
            REQUIRE(p.create(dup) == p.create(fwd));
        }
    }

    SUBCASE("canonical across build paths") {
        // create, repeated insert, and merging two halves must all agree - they exercise different code.
        for (auto n : sizes) {
            INFO("n = ", n);
            auto want = spread(n, 7, 5);

            auto by_insert = S();
            for (auto gid : want)
                by_insert = p.insert(by_insert, elem(gid));

            auto lo = Ref(), hi = Ref();
            for (auto gid : want)
                (lo.size() <= hi.size() ? lo : hi).emplace(gid);
            auto by_merge = p.merge(make(p, lo), make(p, hi));

            auto by_erase = make(p, want);
            by_erase      = p.insert(by_erase, elem(1234567));
            by_erase      = p.erase(by_erase, elem(1234567));

            REQUIRE(by_insert == make(p, want));
            REQUIRE(by_merge == make(p, want));
            REQUIRE(by_erase == make(p, want));
            check(p, by_insert, want);
        }
    }

    SUBCASE("insert") {
        auto want = Ref();
        auto s    = S();

        for (uint32_t i = 0; i != 3 * N + 20; ++i) {
            INFO("i = ", i);
            auto d = elem(i * 11 % 97);
            s      = p.insert(s, d);
            want.emplace(d->gid);
            check(p, s, want);

            CHECK_MESSAGE(p.insert(s, d) == s, "inserting the same element again must be a no-op");
        }
    }

    SUBCASE("erase") {
        for (auto n : sizes) {
            INFO("n = ", n);
            auto want = spread(n);
            auto s    = make(p, want);

            CHECK_MESSAGE(p.erase(s, elem(999999)) == s, "erasing an absent element must be a no-op");

            for (auto gid : spread(n)) {
                s = p.erase(s, elem(gid));
                want.erase(gid);
                check(p, s, want);
            }
            CHECK(s.empty());
        }
    }

    SUBCASE("merge matrix") {
        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                for (uint32_t offset : {uint32_t(0), uint32_t(1), uint32_t(1000)}) { // overlap, interleave, disjoint
                    INFO(n1, " u ", n2, " @", offset);
                    auto w1 = spread(n1);
                    auto w2 = spread(n2, offset);
                    auto s1 = make(p, w1);
                    auto s2 = make(p, w2);

                    auto want = w1;
                    want.insert(w2.begin(), w2.end());

                    auto s = p.merge(s1, s2);
                    check(p, s, want);
                    CHECK_MESSAGE(p.merge(s, s1) == s, "absorption");
                    CHECK_MESSAGE(p.merge(s1, s1) == s1, "idempotence");
                    CHECK_MESSAGE(p.merge(s2, s1) == s, "commutativity");
                }
            }
        }
    }

    SUBCASE("intersect matrix") {
        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                for (uint32_t offset : {uint32_t(0), uint32_t(1), uint32_t(1000)}) {
                    INFO(n1, " ^ ", n2, " @", offset);
                    auto w1 = spread(n1);
                    auto w2 = spread(n2, offset);
                    auto s1 = make(p, w1);
                    auto s2 = make(p, w2);

                    auto want = Ref();
                    for (auto gid : w1)
                        if (w2.contains(gid)) want.emplace(gid);

                    auto s = p.intersect(s1, s2);
                    check(p, s, want);
                    CHECK(p.intersect(s2, s1) == s);
                    CHECK(s1.has_intersection(s2) == !want.empty());
                    CHECK(s2.has_intersection(s1) == !want.empty());
                }
            }
        }
    }

    SUBCASE("diff matrix") {
        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                for (uint32_t offset : {uint32_t(0), uint32_t(1), uint32_t(1000)}) {
                    INFO(n1, " \\ ", n2, " @", offset);
                    auto w1 = spread(n1);
                    auto w2 = spread(n2, offset);
                    auto s1 = make(p, w1);
                    auto s2 = make(p, w2);

                    auto want = Ref();
                    for (auto gid : w1)
                        if (!w2.contains(gid)) want.emplace(gid);

                    check(p, p.diff(s1, s2), want);
                    CHECK(p.diff(s1, s1).empty());
                    CHECK(p.diff(s1, S()) == s1);
                }
            }
        }
    }

    SUBCASE("subset_of") {
        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                INFO(n1, " <= ", n2);
                auto w1   = spread(n1);
                auto w2   = spread(n2);
                auto want = std::ranges::all_of(w1, [&](uint32_t gid) { return w2.contains(gid); });
                CHECK(make(p, w1).subset_of(make(p, w2)) == want);
            }
        }
    }

    SUBCASE("output and for_each") {
        auto want = spread(3 * N);
        auto s    = make(p, want);

        auto seen = Ref();
        s.for_each([&](Elem* d) { seen.emplace(d->gid); });
        CHECK(seen == want);

        auto os = std::ostringstream();
        os << s;
        CHECK(os.str().starts_with("{e0, e2, e4,"));
        CHECK(os.str().ends_with("}"));
        CHECK(&s.stream(os) == &os);

        auto dot = std::ostringstream();
        s.dot(dot);
        CHECK(dot.str().starts_with("digraph {"));
        CHECK(dot.str().find("->") != std::string::npos); // 3 * N elements, so there *are* branches

        auto uniq = std::ostringstream();
        uniq << S(elem(7));
        CHECK(uniq.str() == "{e7}");

        auto empty = std::ostringstream();
        empty << S();
        CHECK(empty.str() == "{}");
    }

    /// Differential test: drive Patricia and std::set through the same random operations.
    SUBCASE("random vs std::set") {
        auto rng  = std::mt19937(42);
        auto gids = std::vector<uint32_t>{0, 1, 2, 3, uint32_t(1) << 31, uint32_t(-1), uint32_t(-2)};
        for (uint32_t i = 0; i != 121; ++i)
            gids.emplace_back(i * 7 % 211);

        auto ref = std::vector<Ref>{{}};
        auto got = std::vector<S>{{}};

        for (int step = 0; step != 4000; ++step) {
            INFO("step = ", step);
            auto i  = rng() % got.size();
            auto j  = rng() % got.size();
            auto op = rng() % 6;
            INFO("op = ", op);

            auto d = elem(gids[rng() % gids.size()]);
            auto r = ref[i];

            if (op == 0) {
                got.emplace_back(p.insert(got[i], d));
                r.emplace(d->gid);
            } else if (op == 1) {
                got.emplace_back(p.erase(got[i], d));
                r.erase(d->gid);
            } else if (op == 2) {
                got.emplace_back(p.merge(got[i], got[j]));
                r.insert(ref[j].begin(), ref[j].end());
            } else if (op == 3) {
                got.emplace_back(p.intersect(got[i], got[j]));
                std::erase_if(r, [&](uint32_t gid) { return !ref[j].contains(gid); });
            } else if (op == 4) {
                got.emplace_back(p.diff(got[i], got[j]));
                std::erase_if(r, [&](uint32_t gid) { return ref[j].contains(gid); });
            } else { // queries only
                REQUIRE(got[i].contains(d) == ref[i].contains(d->gid));

                auto meets = std::ranges::any_of(ref[i], [&](uint32_t gid) { return ref[j].contains(gid); });
                REQUIRE(got[i].has_intersection(got[j]) == meets);

                auto sub = std::ranges::all_of(ref[i], [&](uint32_t gid) { return ref[j].contains(gid); });
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

namespace {

/// The defaults: 64-bit ids and 8 elements per array node.
struct alignas(8) Big {
    uint64_t gid;
};

struct BigKey {
    static uint64_t key(const Big* b) noexcept { return b->gid; }
};

} // namespace

TEST_CASE("Patricia: 64-bit ids") {
    auto pool = std::map<uint64_t, Big>();
    auto big  = [&pool](uint64_t gid) { return &pool.emplace(gid, Big{gid}).first->second; };

    auto p    = fe::Patricia<Big, BigKey, uint64_t>();
    auto want = std::set<uint64_t>{0, 1, uint64_t(1) << 32, uint64_t(1) << 63, uint64_t(-2), uint64_t(-1)};
    for (uint64_t i = 0; i != 100; ++i)
        want.emplace(i * 37 % 211);

    auto v = std::vector<Big*>();
    for (auto gid : want)
        v.emplace_back(big(gid));
    auto s = p.create(v);

    auto got = std::vector<uint64_t>();
    for (auto b : s)
        got.emplace_back(b->gid);
    CHECK(std::equal(got.begin(), got.end(), want.begin(), want.end()));
    CHECK(s.size() == want.size());
    CHECK(s.min() == big(0));
    CHECK(s.max() == big(uint64_t(-1)));
    CHECK(s.contains(big(uint64_t(1) << 63)));

    auto os = std::ostringstream();
    os << p.create({big(1), big(uint64_t(1) << 63)});
    CHECK(os.str() == "{1, 9223372036854775808}"); // no Key::stream, so the ids speak for themselves

    for (auto gid : want)
        s = p.erase(s, big(gid));
    CHECK(s.empty());
}
