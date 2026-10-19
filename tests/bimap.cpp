#include <cstdint>

#include <map>
#include <random>
#include <string>
#include <string_view>

#include <doctest/doctest.h>
#include <fe/bimap.h>
#include <fe/container.h>
#include <fe/hash.h>

namespace {
template<template<class...> class B>
using IntStr
    = B<int, std::string, ankerl::unordered_dense::hash<int>, std::equal_to<int>, fe::StrHash, std::equal_to<>>;
} // namespace

TEST_CASE_TEMPLATE("BiMap", M, IntStr<fe::BiMap>, IntStr<fe::BiNodeMap>) {
    auto bm = M();
    CHECK(bm.empty());

    CHECK(bm.insert(1, "one"));
    CHECK(bm.insert(2, "two"));
    CHECK(!bm.insert(1, "uno"));
    CHECK(!bm.insert(3, "two"));
    CHECK(bm.size() == 2);
    CHECK(bm.at_left(1) == "one");
    CHECK(bm.at_right("two") == 2);
    CHECK(!bm.contains_right("uno"));
    CHECK(!bm.contains_left(3));
    CHECK_THROWS_AS((void)bm.at_right("three"), std::out_of_range);

    SUBCASE("heterogeneous lookup") {
        auto sv = std::string_view("onetwo").substr(0, 3);
        CHECK(bm.at_right(sv) == 1);
        CHECK(bm.erase_right(sv));
        CHECK(!bm.contains_left(1));
    }

    SUBCASE("erase") {
        CHECK(bm.erase_left(1));
        CHECK(!bm.erase_left(1));
        CHECK(!bm.contains_right("one"));
        CHECK(bm.at_right("two") == 2);
        CHECK(bm.erase_right("two"));
        CHECK(!bm.contains_left(2));
        CHECK(bm.empty());
    }

    SUBCASE("insert_or_assign") {
        bm.insert_or_assign(1, "two");
        CHECK(bm.size() == 1);
        CHECK(bm.at_left(1) == "two");
        CHECK(bm.at_right("two") == 1);
        CHECK(!bm.contains_left(2));
        CHECK(!bm.contains_right("one"));

        // Arguments that alias entries of the map itself must survive the erase.
        bm.insert(3, "three");
        bm.insert_or_assign(bm.at_right("three"), bm.at_left(1));
        CHECK(bm.size() == 1);
        CHECK(bm.at_left(3) == "two");
        CHECK(bm.at_right("two") == 3);
    }

    SUBCASE("iterate, copy, move, compare") {
        size_t n = 0;
        for (const auto& [l, r] : bm) {
            CHECK(bm.at_right(r) == l);
            ++n;
        }
        CHECK(n == 2);

        auto copy = bm;
        CHECK(copy == bm);
        copy.erase_left(1);
        CHECK(copy != bm);
        CHECK(bm.at_right("one") == 1);

        auto moved = std::move(copy);
        CHECK(moved.at_right("two") == 2);
        copy = moved;
        moved.clear();
        CHECK(moved.empty());
        CHECK(copy.at_right("two") == 2);
        CHECK(!copy.contains_right("one"));
    }
}

TEST_CASE("BiMap::right") {
    auto bm = IntStr<fe::BiMap>();
    bm.insert(1, "one");
    CHECK(*fe::lookup(bm.right(), std::string_view("one")) == 1);
}

TEST_CASE("BiNodeMap") {
    SUBCASE("pairs stay put on insert") {
        auto bm = fe::BiNodeMap<int, std::string>();
        bm.insert(0, "0");
        auto p = bm.find_right(std::string("0"));
        for (int i = 1; i != 10'000; ++i)
            bm.insert(i, std::to_string(i));
        CHECK(bm.find_right(std::string("0")) == p);

        auto moved = std::move(bm);
        CHECK(moved.find_right(std::string("0")) == p);
    }

    SUBCASE("random") {
        auto bm  = fe::BiNodeMap<uint32_t, uint32_t>();
        auto l2r = std::map<uint32_t, uint32_t>();
        auto r2l = std::map<uint32_t, uint32_t>();
        auto rng = std::mt19937(23);
        auto key = std::uniform_int_distribution<uint32_t>(0, 255);

        for (int i = 0; i != 20'000; ++i) {
            auto l = key(rng), r = key(rng);
            switch (rng() % 4) {
                case 0: {
                    bool ins = !l2r.contains(l) && !r2l.contains(r);
                    if (ins) l2r[l] = r, r2l[r] = l;
                    CHECK(bm.insert(l, r) == ins);
                    break;
                }
                case 1:
                    if (auto j = l2r.find(l); j != l2r.end()) r2l.erase(j->second), l2r.erase(j);
                    if (auto j = r2l.find(r); j != r2l.end()) l2r.erase(j->second), r2l.erase(j);
                    l2r[l] = r, r2l[r] = l;
                    bm.insert_or_assign(l, r);
                    break;
                case 2:
                    if (auto j = l2r.find(l); j != l2r.end()) r2l.erase(j->second), l2r.erase(j);
                    bm.erase_left(l);
                    break;
                case 3:
                    if (auto j = r2l.find(r); j != r2l.end()) l2r.erase(j->second), r2l.erase(j);
                    bm.erase_right(r);
                    break;
            }
        }

        REQUIRE(bm.size() == l2r.size());
        for (auto [l, r] : l2r) {
            CHECK(bm.at_left(l) == r);
            CHECK(bm.at_right(r) == l);
        }
    }
}
