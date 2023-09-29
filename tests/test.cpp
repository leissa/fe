#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <fe/ring.h>

#include <doctest/doctest.h>

TEST_CASE("ring1") {
    fe::Ring<int, 1> ring;
    ring.put(0);
    CHECK(ring.front() == 0);
    ring.put(1);
    CHECK(ring.front() == 1);
}

TEST_CASE("ring3") {
    fe::Ring<int, 3> ring;
    ring.put(0).put(1).put(2);
    CHECK(ring[0] == 0);
    CHECK(ring[1] == 1);
    CHECK(ring[2] == 2);
    ring.put(3);
    CHECK(ring[0] == 1);
    CHECK(ring[1] == 2);
    CHECK(ring[2] == 3);
    ring.put(4);
    CHECK(ring[0] == 2);
    CHECK(ring[1] == 3);
    CHECK(ring[2] == 4);
    ring.put(5);
    CHECK(ring[0] == 3);
    CHECK(ring[1] == 4);
    CHECK(ring[2] == 5);
}
