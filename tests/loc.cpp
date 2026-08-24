#include <sstream>

#include <doctest/doctest.h>
#include <fe/driver.h>
#include <fe/format.h>
#include <fe/loc.h>
#include <fe/src.h>

using fe::Loc;
using fe::Pos;

TEST_CASE("Pos") {
    CHECK(!Pos());
    CHECK(Pos(0)); // 0 is the first byte of a file - not the invalid sentinel
    CHECK(Pos(3) < Pos(4));
    CHECK(Pos(3) + 4 == Pos(7));

    // Default operator<< from fe/loc.cpp.h (included once in lexer.cpp).
    CHECK(std::format("{}", Pos(3)) == "3");
    CHECK(std::format("{}", Pos()) == "<unknown position>");
}

TEST_CASE("Loc") {
    std::filesystem::path path("test.let");

    CHECK(!Loc());
    CHECK(Loc(Pos(0), Pos(4)));

    SUBCASE("anew, size and operator+") {
        Loc loc(&path, Pos(2), Pos(9));
        CHECK(loc.size() == 7);
        CHECK(loc.anew_begin() == Loc(&path, Pos(2)));
        CHECK(loc.anew_end() == Loc(&path, Pos(9)));
        CHECK(loc + Pos(20) == Loc(&path, Pos(2), Pos(20)));
        CHECK(loc + Loc(&path, Pos(30), Pos(40)) == Loc(&path, Pos(2), Pos(40)));
    }

    SUBCASE("operator& intersects") {
        std::filesystem::path other("other.let");

        // Overlapping and nested ranges yield the shared part.
        CHECK((Loc(&path, Pos(1), Pos(9)) & Loc(&path, Pos(5), Pos(20))) == Loc(&path, Pos(5), Pos(9)));
        CHECK((Loc(&path, Pos(5), Pos(20)) & Loc(&path, Pos(1), Pos(9))) == Loc(&path, Pos(5), Pos(9)));
        CHECK((Loc(&path, Pos(1), Pos(99)) & Loc(&path, Pos(20), Pos(30))) == Loc(&path, Pos(20), Pos(30)));

        // Touching, disjoint, different file, empty, or either side invalid: no overlap.
        CHECK(!(Loc(&path, Pos(1), Pos(5)) & Loc(&path, Pos(5), Pos(9))));
        CHECK(!(Loc(&path, Pos(1), Pos(4)) & Loc(&path, Pos(5), Pos(9))));
        CHECK(!(Loc(&path, Pos(1), Pos(9)) & Loc(&other, Pos(1), Pos(9))));
        CHECK(!(Loc(&path, Pos(5)) & Loc(&path, Pos(1), Pos(9))));
        CHECK(!(Loc(&path, Pos(1), Pos(9)) & Loc()));
        CHECK(!(Loc() & Loc()));

        // Dual to operator+: the hull of two ranges contains their overlap.
        auto a = Loc(&path, Pos(1), Pos(9));
        auto b = Loc(&path, Pos(5), Pos(20));
        CHECK(((a + b) & (a & b)) == (a & b));
    }

    SUBCASE("equality compares paths by pointer") {
        std::filesystem::path same("test.let");
        CHECK(Loc(&path, Pos(2)) == Loc(&path, Pos(2)));
        CHECK(Loc(&path, Pos(2)) != Loc(&same, Pos(2)));
        CHECK(Loc(Pos(2), Pos(2)) != Loc(&path, Pos(2)));
    }

    SUBCASE("stream output falls back to raw offsets") {
        CHECK(std::format("{}", Loc(&path, Pos(2), Pos(9))) == "test.let@2-9");
        CHECK(std::format("{}", Loc(&path, Pos(2))) == "test.let@2");
        CHECK(std::format("{}", Loc(Pos(2), Pos(2))) == "<unknown file>@2");
        CHECK(std::format("{}", Loc()) == "<unknown location>");
    }
}

TEST_CASE("SrcFile") {
    auto file = fe::SrcFile("test.let", "let x = 1;\nlet λ = «2»;\r\n\nlast");
    // Row 2 starts at offset 11; λ occupies [15, 17), « [20, 22) and » [23, 25).

    CHECK(file.num_rows() == 4);
    CHECK(file.begin() == Pos(0));
    CHECK(file.end() == Pos(file.buf().size()));

    SUBCASE("rows and columns") {
        CHECK(file.row(Pos(0)) == 1);
        CHECK(file.col(Pos(0)) == 1);
        CHECK(file.row(Pos(9)) == 1);
        CHECK(file.col(Pos(9)) == 10);
        CHECK(file.row(Pos(11)) == 2);
        CHECK(file.col(Pos(11)) == 1);

        // A column counts code points, not bytes: `λ`, `«` and `»` are two bytes each.
        CHECK(file.col(Pos(15)) == 5);  // λ
        CHECK(file.col(Pos(17)) == 6);  // ' '
        CHECK(file.col(Pos(20)) == 9);  // «
        CHECK(file.col(Pos(23)) == 11); // »
        CHECK(file.row(Pos(23)) == 2);

        // The lexer only ever hands out code point boundaries; an interior byte still resolves,
        // counting the partial sequence as one code point.
        CHECK(file.col(Pos(16)) == 6);

        CHECK(file.row(Pos()) == 0);
        CHECK(file.col(Pos()) == 0);
        CHECK(file.row(file.end()) == 4);
        CHECK(file.row(Pos(9999)) == 0);
    }

    SUBCASE("lines drop their terminator") {
        CHECK(file.line(1) == "let x = 1;");
        CHECK(file.line(2) == "let λ = «2»;"); // \r\n
        CHECK(file.line(3) == "");
        CHECK(file.line(4) == "last");
        CHECK(file.line(0) == "");
        CHECK(file.line(5) == "");
    }

    SUBCASE("prev steps back one whole code point") {
        CHECK(file.prev(Pos(1)) == Pos(0));
        CHECK(file.prev(Pos(17)) == Pos(15)); // λ occupies [15, 17)
        CHECK(file.prev(Pos(0)) == Pos(0));
    }
}

TEST_CASE("SrcMap") {
    fe::SrcMap map;
    auto [file, fresh] = map.add("test.let", "let x = 1;\nlet y = 2;");
    CHECK(fresh);
    CHECK(map.add("test.let", "whatever") == std::pair(file, false));
    CHECK(map.lookup(file->path()) == file);
    CHECK(map.lookup(Loc()) == nullptr);

    // A resolvable Loc renders as `file:row:col-row:col`; the trailing position is the *last*
    // character - not the one Loc::end points past.
    CHECK(std::format("{}", map.at(Loc(file->path(), Pos(4), Pos(9)))) == "test.let:1:5-1:9");
    CHECK(std::format("{}", map.at(Loc(file->path(), Pos(4), Pos(5)))) == "test.let:1:5");
    CHECK(std::format("{}", map.at(Loc(file->path(), Pos(4)))) == "test.let:1:5");
    CHECK(std::format("{}", map.at(Loc(file->path(), Pos(4), Pos(15)))) == "test.let:1:5-2:4");

    // An unregistered file has no rows to report, so this degrades to Loc's own output.
    std::filesystem::path other("other.let");
    CHECK(std::format("{}", map.at(Loc(&other, Pos(4), Pos(9)))) == "other.let@4-9");
    CHECK(std::format("{}", map.at(Loc())) == "<unknown location>");
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
    auto [file, _] = drv.src().add("test.let", "let x = 1;\nlet y = 2;\nlet z = 3;");
    auto path      = file->path();

    CHECK(drv.num_errors() == 0);
    CHECK(drv.num_warnings() == 0);

    drv.err(Loc(path, Pos(4), Pos(5)), "expected '{}'", ';');
    drv.warn(Loc(path, Pos(15), Pos(16)), "unused variable '{}'", "x");
    drv.note(Loc(path, Pos(26), Pos(27)), "declared here");

    CHECK(drv.num_errors() == 1);
    CHECK(drv.num_warnings() == 1);
    CHECK(capture.oss.str()
          == "test.let:1:5: error: expected ';'\n"
             "test.let:2:5: warning: unused variable 'x'\n"
             "test.let:3:5: note: declared here\n");

    // Driver inherits SymPool.
    CHECK(drv.sym("foo") == drv.sym("foo"));
}
