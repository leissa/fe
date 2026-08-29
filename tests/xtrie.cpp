#include <cstdint>

#include <deque>
#include <map>
#include <random>
#include <set>
#include <vector>

#include <doctest/doctest.h>
#include <fe/xtrie.h>

namespace {

/// Minimal element: fe::XTrie only needs a gid for ordering/hashing and a writable tid.
struct Elem {
    explicit Elem(uint32_t gid)
        : gid_(gid) {}

    uint32_t gid_;
    uint32_t tid_ = 0;
};

struct Key {
    static uint32_t gid(const Elem* e) noexcept { return e->gid_; }
    static uint32_t tid(const Elem* e) noexcept { return e->tid_; }
    static void set_tid(Elem* e, uint32_t tid) noexcept { e->tid_ = tid; }
};

using S                   = fe::XTrie<Elem, Key>;
static constexpr size_t N = 16; ///< XTrie's Data/Node switch-over point.

/// Stable storage, interned by gid: XTrie keys on pointers but *orders* by gid, so one gid must map to
/// exactly one Elem - otherwise the sortedness/uniqueness invariant is violated by construction.
struct Pool {
    Elem* operator()(uint32_t gid) {
        auto [i, ins] = gid2elem.emplace(gid, nullptr);
        if (ins) i->second = &elems.emplace_back(gid);
        return i->second;
    }

    std::deque<Elem> elems;
    std::map<uint32_t, Elem*> gid2elem;
};

std::set<uint32_t> gids(S::Set s) {
    auto res = std::set<uint32_t>();
    for (auto e : s)
        res.emplace(e->gid_);
    return res;
}

S::Set make(S& sets, Pool& pool, std::set<uint32_t> want) {
    auto v = std::vector<Elem*>();
    for (auto g : want)
        v.emplace_back(pool(g));
    return sets.create(v);
}

} // namespace

TEST_CASE("XTrie") {
    auto sets = S();
    auto pool = Pool();

    SUBCASE("flavours") {
        CHECK(make(sets, pool, {}).empty());
        CHECK(make(sets, pool, {}).size() == 0);

        for (size_t n : {size_t(1), size_t(2), N - 1, N, N + 1, 2 * N, 3 * N}) {
            INFO("n = ", n);
            auto want = std::set<uint32_t>();
            for (uint32_t i = 0; i != n; ++i)
                want.emplace(i * 7 + 1); // arbitrary, distinct
            auto s = make(sets, pool, want);
            CHECK(s.size() == n);
            CHECK(gids(s) == want);
            for (auto g : want) {
                INFO("gid = ", g);
                CHECK(s.contains(pool(g)));
            }
        }
    }

    SUBCASE("create dedups and is order independent") {
        // Equal contents must yield the *same* Set - Data is hash-consed, trie paths are canonical.
        for (size_t n : {size_t(2), N, N + 1, 2 * N}) {
            INFO("n = ", n);
            auto fwd = std::vector<Elem*>();
            auto rev = std::vector<Elem*>();
            for (uint32_t i = 0; i != n; ++i)
                fwd.emplace_back(pool(i * 3 + 5));

            for (auto i = fwd.rbegin(), ie = fwd.rend(); i != ie; ++i)
                rev.emplace_back(*i);
            rev.emplace_back(fwd.front()); // and a duplicate for good measure

            auto s1 = sets.create(fwd);
            auto s2 = sets.create(rev);
            CHECK(s1 == s2);
            CHECK(s1.size() == n);
        }
    }

    SUBCASE("insert") {
        auto want = std::set<uint32_t>();
        auto s    = S::Set();
        for (uint32_t i = 0; i != 3 * N; ++i) {
            INFO("i = ", i);
            auto g = (i * 11 + 3) % 97;
            auto e = pool(g);
            s      = sets.insert(s, e);
            want.emplace(g);
            CHECK(gids(s) == want);

            CHECK_MESSAGE(sets.insert(s, e) == s, "insert must be idempotent");
        }
    }

    SUBCASE("erase") {
        for (size_t n : {size_t(1), size_t(2), N, N + 1, 2 * N}) {
            INFO("n = ", n);
            auto want = std::set<uint32_t>();
            auto es   = std::vector<Elem*>();
            for (uint32_t i = 0; i != n; ++i)
                want.emplace(i);
            auto s = make(sets, pool, want);
            for (auto g : want)
                es.emplace_back(pool(g));

            CHECK_MESSAGE(sets.erase(s, pool(1000 + n)) == s, "erasing something that is not in there is a no-op");

            for (auto e : es) {
                s = sets.erase(s, e);
                want.erase(e->gid_);
                CHECK(gids(s) == want);
            }
            CHECK(s.empty());
        }
    }

    /// The 4x4 flavour cross product for merge - this is what the linear array union has to get right.
    SUBCASE("merge flavour matrix") {
        auto sizes = {size_t(0), size_t(1), size_t(2), N - 1, N, N + 1, 2 * N};

        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                // disjoint, overlapping, interleaved
                for (uint32_t offset : {uint32_t(0), uint32_t(1), uint32_t(1000)}) {
                    INFO(n1, " u ", n2, " @", offset);
                    auto w1 = std::set<uint32_t>();
                    auto w2 = std::set<uint32_t>();
                    for (uint32_t i = 0; i != n1; ++i)
                        w1.emplace(2 * i);
                    for (uint32_t i = 0; i != n2; ++i)
                        w2.emplace(2 * i + offset);

                    auto s1 = make(sets, pool, w1);
                    auto s2 = make(sets, pool, w2);

                    auto want = w1;
                    want.insert(w2.begin(), w2.end());

                    auto m = sets.merge(s1, s2);
                    CHECK(gids(m) == want);
                    CHECK(m.size() == want.size());
                    CHECK_MESSAGE(sets.merge(s2, s1) == m, "merge must be commutative"); // canonical => same Set
                    CHECK_MESSAGE(sets.merge(m, s1) == m, "absorption");
                }
            }
        }
    }

    SUBCASE("has_intersection") {
        auto sizes = {size_t(1), size_t(2), N, N + 1, 2 * N};

        for (auto n1 : sizes) {
            for (auto n2 : sizes) {
                for (uint32_t offset : {uint32_t(0), uint32_t(1), uint32_t(10000)}) {
                    INFO(n1, " ^ ", n2, " @", offset);
                    auto w1 = std::set<uint32_t>();
                    auto w2 = std::set<uint32_t>();
                    for (uint32_t i = 0; i != n1; ++i)
                        w1.emplace(2 * i);
                    for (uint32_t i = 0; i != n2; ++i)
                        w2.emplace(2 * i + offset);

                    auto want = false;
                    for (auto g : w1)
                        want |= w2.contains(g);

                    auto s1 = make(sets, pool, w1);
                    auto s2 = make(sets, pool, w2);
                    CHECK(s1.has_intersection(s2) == want);
                    CHECK(s2.has_intersection(s1) == want);
                }
            }
        }
    }

    /// Differential test: drive XTrie and std::set through the same random operations.
    SUBCASE("random vs std::set") {
        auto rng = std::mt19937(42);

        auto elems = std::vector<Elem*>();
        for (uint32_t g = 0; g != 128; ++g)
            elems.emplace_back(pool(g));

        auto ref = std::vector<std::set<uint32_t>>{{}};
        auto got = std::vector<S::Set>{{}};

        for (int step = 0; step != 20000; ++step) {
            INFO("step = ", step);
            auto i  = rng() % got.size();
            auto op = rng() % 4;
            INFO("op = ", op);

            if (op == 0) { // insert
                auto e = elems[rng() % elems.size()];
                got.emplace_back(sets.insert(got[i], e));
                auto r = ref[i];
                r.emplace(e->gid_);
                ref.emplace_back(std::move(r));
            } else if (op == 1) { // erase
                auto e = elems[rng() % elems.size()];
                got.emplace_back(sets.erase(got[i], e));
                auto r = ref[i];
                r.erase(e->gid_);
                ref.emplace_back(std::move(r));
            } else if (op == 2) { // merge
                auto j = rng() % got.size();
                got.emplace_back(sets.merge(got[i], got[j]));
                auto r = ref[i];
                r.insert(ref[j].begin(), ref[j].end());
                ref.emplace_back(std::move(r));
            } else { // check membership / intersection against the reference
                auto j = rng() % got.size();
                auto e = elems[rng() % elems.size()];
                REQUIRE(got[i].contains(e) == ref[i].contains(e->gid_));

                auto want = false;
                for (auto g : ref[i])
                    want |= ref[j].contains(g);
                REQUIRE(got[i].has_intersection(got[j]) == want);
                continue;
            }

            REQUIRE(gids(got.back()) == ref.back());
            REQUIRE(got.back().size() == ref.back().size());

            if (got.size() > 64) { // keep the working set bounded
                got.erase(got.begin() + 1, got.begin() + 32);
                ref.erase(ref.begin() + 1, ref.begin() + 32);
            }
        }
    }
}
