#include <sstream>

#include <doctest/doctest.h>
#include <fe/driver.h>
#include <fe/format.h>
#include <fe/loc.h>

using fe::Loc;
using fe::Pos;

TEST_CASE("Pos") {
    CHECK(!Pos());
    CHECK(Pos(1, 1));
    CHECK(Pos(3) == Pos(3, 0));
    CHECK(Pos(1, 2) < Pos(1, 3));
    CHECK(Pos(1, 9) < Pos(2, 1));

    // Default operator<< from fe/loc.cpp.h (included once in lexer.cpp).
    CHECK(std::format("{}", Pos(3, 7)) == "3:7");
    CHECK(std::format("{}", Pos(3)) == "3");
    CHECK(std::format("{}", Pos()) == "<unknown position>");
}

TEST_CASE("Loc") {
    std::filesystem::path path("test.let");

    CHECK(!Loc());
    CHECK(Loc({1, 1}, {1, 5}));

    SUBCASE("anew and operator+") {
        Loc loc(&path, {1, 2}, {3, 4});
        CHECK(loc.anew_begin() == Loc(&path, {1, 2}, {1, 2}));
        CHECK(loc.anew_finis() == Loc(&path, {3, 4}, {3, 4}));
        CHECK(loc + Pos(5, 6) == Loc(&path, {1, 2}, {5, 6}));
        CHECK(loc + Loc(&path, {7, 8}, {9, 10}) == Loc(&path, {1, 2}, {9, 10}));
    }

    SUBCASE("operator& intersects") {
        std::filesystem::path other("other.let");

        // Overlapping, nested and touching spans all yield the shared part.
        CHECK((Loc(&path, {1, 1}, {1, 9}) & Loc(&path, {1, 5}, {1, 20})) == Loc(&path, {1, 5}, {1, 9}));
        CHECK((Loc(&path, {1, 5}, {1, 20}) & Loc(&path, {1, 1}, {1, 9})) == Loc(&path, {1, 5}, {1, 9}));
        CHECK((Loc(&path, {1, 1}, {9, 9}) & Loc(&path, {2, 1}, {3, 1})) == Loc(&path, {2, 1}, {3, 1}));
        CHECK((Loc(&path, {1, 1}, {1, 5}) & Loc(&path, {1, 5}, {1, 9})) == Loc(&path, {1, 5}, {1, 5}));

        // Disjoint, different file, or either side invalid: no overlap.
        CHECK(!(Loc(&path, {1, 1}, {1, 4}) & Loc(&path, {1, 5}, {1, 9})));
        CHECK(!(Loc(&path, {1, 1}, {1, 9}) & Loc(&other, {1, 1}, {1, 9})));
        CHECK(!(Loc(&path, {1, 1}, {1, 9}) & Loc()));
        CHECK(!(Loc() & Loc()));

        // Dual to operator+: the hull of two spans contains their overlap.
        auto a = Loc(&path, {1, 1}, {1, 9});
        auto b = Loc(&path, {1, 5}, {1, 20});
        CHECK(((a + b) & (a & b)) == (a & b));
    }

    SUBCASE("equality compares paths by pointer") {
        std::filesystem::path same("test.let");
        CHECK(Loc(&path, {1, 2}) == Loc(&path, {1, 2}));
        CHECK(Loc(&path, {1, 2}) != Loc(&same, {1, 2}));
        CHECK(Loc({1, 2}, {1, 2}) != Loc(&path, {1, 2}));
    }

    SUBCASE("stream output") {
        CHECK(std::format("{}", Loc(&path, {1, 2}, {3, 4})) == "test.let:1:2-3:4");
        CHECK(std::format("{}", Loc(&path, {1, 2}, {1, 2})) == "test.let:1:2");
        CHECK(std::format("{}", Loc({1, 2}, {1, 2})) == "<unknown file>:1:2");
        CHECK(std::format("{}", Loc()) == "<unknown location>");
    }
}

TEST_CASE("Driver") {
    // Capture std::cerr and disable colors so the diagnostic text is predictable.
    struct Capture {
        Capture()
            : old_buf(std::cerr.rdbuf(oss.rdbuf()))
            , old_mode(fe::term::mode()) {
            fe::term::set_mode(fe::term::Mode::Never);
        }
        ~Capture() {
            std::cerr.rdbuf(old_buf);
            fe::term::set_mode(old_mode);
        }

        std::ostringstream oss;
        std::streambuf* old_buf;
        fe::term::Mode old_mode;
    } capture;

    fe::Driver drv;
    std::filesystem::path path("test.let");

    CHECK(drv.num_errors() == 0);
    CHECK(drv.num_warnings() == 0);

    drv.err(Loc(&path, {1, 2}), "expected '{}'", ';');
    drv.warn(Loc(&path, {2, 3}), "unused variable '{}'", "x");
    drv.note(Loc(&path, {3, 4}), "declared here");

    CHECK(drv.num_errors() == 1);
    CHECK(drv.num_warnings() == 1);
    CHECK(capture.oss.str()
          == "test.let:1:2: error: expected ';'\n"
             "test.let:2:3: warning: unused variable 'x'\n"
             "test.let:3:4: note: declared here\n");

    // Driver inherits SymPool.
    CHECK(drv.sym("foo") == drv.sym("foo"));
}
