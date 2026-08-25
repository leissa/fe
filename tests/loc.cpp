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
    auto src = fe::Src("test.let", "");

    CHECK(!Loc());
    CHECK(Loc(Pos(0), Pos(4)));

    SUBCASE("anew, size and operator+") {
        Loc loc(&src, Pos(2), Pos(9));
        CHECK(loc.size() == 7);
        CHECK(loc.anew_begin() == Loc(&src, Pos(2)));
        CHECK(loc.anew_end() == Loc(&src, Pos(9)));
        CHECK(loc + Pos(20) == Loc(&src, Pos(2), Pos(20)));
        CHECK(loc + Loc(&src, Pos(30), Pos(40)) == Loc(&src, Pos(2), Pos(40)));
    }

    SUBCASE("operator& intersects") {
        auto other = fe::Src("other.let", "");

        // Overlapping and nested ranges yield the shared part.
        CHECK((Loc(&src, Pos(1), Pos(9)) & Loc(&src, Pos(5), Pos(20))) == Loc(&src, Pos(5), Pos(9)));
        CHECK((Loc(&src, Pos(5), Pos(20)) & Loc(&src, Pos(1), Pos(9))) == Loc(&src, Pos(5), Pos(9)));
        CHECK((Loc(&src, Pos(1), Pos(99)) & Loc(&src, Pos(20), Pos(30))) == Loc(&src, Pos(20), Pos(30)));

        // Touching, disjoint, different file, empty, or either side invalid: no overlap.
        CHECK(!(Loc(&src, Pos(1), Pos(5)) & Loc(&src, Pos(5), Pos(9))));
        CHECK(!(Loc(&src, Pos(1), Pos(4)) & Loc(&src, Pos(5), Pos(9))));
        CHECK(!(Loc(&src, Pos(1), Pos(9)) & Loc(&other, Pos(1), Pos(9))));
        CHECK(!(Loc(&src, Pos(5)) & Loc(&src, Pos(1), Pos(9))));
        CHECK(!(Loc(&src, Pos(1), Pos(9)) & Loc()));
        CHECK(!(Loc() & Loc()));

        // Dual to operator+: the hull of two ranges contains their overlap.
        auto a = Loc(&src, Pos(1), Pos(9));
        auto b = Loc(&src, Pos(5), Pos(20));
        CHECK(((a + b) & (a & b)) == (a & b));
    }

    SUBCASE("equality compares sources by pointer") {
        auto same = fe::Src("test.let", "");
        CHECK(Loc(&src, Pos(2)) == Loc(&src, Pos(2)));
        CHECK(Loc(&src, Pos(2)) != Loc(&same, Pos(2)));
        CHECK(Loc(Pos(2), Pos(2)) != Loc(&src, Pos(2)));
    }

    SUBCASE("stream output falls back to raw offsets") {
        // `src` is empty, so it has no row/column for any of these - see TEST_CASE("SrcMap").
        CHECK(std::format("{}", Loc(&src, Pos(2), Pos(9))) == "test.let@2-9");
        CHECK(std::format("{}", Loc(&src, Pos(2))) == "test.let@2");
        CHECK(std::format("{}", Loc(Pos(2), Pos(2))) == "<unknown file>@2");
        CHECK(std::format("{}", Loc()) == "<unknown location>");
    }
}

TEST_CASE("Src") {
    auto src = fe::Src("test.let", "let x = 1;\nlet λ = «2»;\r\n\nlast");
    // Row 2 starts at offset 11; λ occupies [15, 17), « [20, 22) and » [23, 25).

    CHECK(src.num_rows() == 4);
    CHECK(src.begin() == Pos(0));
    CHECK(src.end() == Pos(src.buf().size()));

    SUBCASE("rows and columns") {
        CHECK(src.row(Pos(0)) == 1);
        CHECK(src.col(Pos(0)) == 1);
        CHECK(src.row(Pos(9)) == 1);
        CHECK(src.col(Pos(9)) == 10);
        CHECK(src.row(Pos(11)) == 2);
        CHECK(src.col(Pos(11)) == 1);

        // A column counts code points, not bytes: `λ`, `«` and `»` are two bytes each.
        CHECK(src.col(Pos(15)) == 5);  // λ
        CHECK(src.col(Pos(17)) == 6);  // ' '
        CHECK(src.col(Pos(20)) == 9);  // «
        CHECK(src.col(Pos(23)) == 11); // »
        CHECK(src.row(Pos(23)) == 2);

        // The lexer only ever hands out code point boundaries; an interior byte still resolves,
        // counting the partial sequence as one code point.
        CHECK(src.col(Pos(16)) == 6);

        CHECK(src.row(Pos()) == 0);
        CHECK(src.col(Pos()) == 0);
        CHECK(src.row(src.end()) == 4);
        CHECK(src.row(Pos(9999)) == 0);
    }

    SUBCASE("lines drop their terminator") {
        CHECK(src.line(1) == "let x = 1;");
        CHECK(src.line(2) == "let λ = «2»;"); // \r\n
        CHECK(src.line(3) == "");
        CHECK(src.line(4) == "last");
        CHECK(src.line(0) == "");
        CHECK(src.line(5) == "");
    }

    SUBCASE("prev steps back one whole code point") {
        CHECK(src.prev(Pos(1)) == Pos(0));
        CHECK(src.prev(Pos(17)) == Pos(15)); // λ occupies [15, 17)
        CHECK(src.prev(Pos(0)) == Pos(0));
    }

    SUBCASE("malformed UTF-8 resolves the way utf8::decode reads it") {
        // A lone continuation byte is its own invalid code point - not part of the `a` before it.
        auto bad = fe::Src("bad.let", "a\x80"
                                      "\x80"
                                      "b");
        CHECK(bad.prev(Pos(2)) == Pos(1));
        CHECK(bad.prev(Pos(3)) == Pos(2));
        CHECK(bad.col(Pos(3)) == 4);

        // A truncated lead byte does not swallow what follows it.
        auto trunc = fe::Src("trunc.let", "\xc2"
                                          "ab");
        CHECK(trunc.col(Pos(1)) == 2);
        CHECK(trunc.col(Pos(2)) == 3);
        CHECK(trunc.prev(Pos(2)) == Pos(1));
    }
}

TEST_CASE("SrcMap") {
    fe::SrcMap map;
    auto [src, fresh] = map.add("test.let", "let x = 1;\nlet y = 2;");
    CHECK(fresh);
    CHECK(map.add("test.let", "whatever") == std::pair(src, false));
    CHECK(map.lookup("test.let") == src);
    CHECK(map.lookup("other.let") == nullptr);
    CHECK(map.add("nonexistent.let").first == nullptr);

    // Different spellings of the same file share one Src - that is what makes Loc::src
    // comparable by pointer. The path a Src reports back is the one it was registered with.
    CHECK(map.lookup("./test.let") == src);
    CHECK(map.lookup("sub/../test.let") == src);
    CHECK(map.add("./test.let", "whatever") == std::pair(src, false));
    CHECK(src->path() == "test.let");

    // The same file spelled two ways is also one entry if it really sits on disk - the branch where
    // SrcMap::key has to consult the file system. Everything Loc::src compares rests on this.
    auto self           = std::filesystem::path(__FILE__);
    auto [me, me_fresh] = map.add(self);
    CHECK(me_fresh);
    CHECK(me->buf().starts_with("#include"));
    CHECK(map.lookup(self.parent_path() / "." / self.filename()) == me);
    CHECK(map.lookup(self.parent_path() / "nonexistent" / ".." / self.filename()) == me);

    // A Loc resolves through its Src as `path:row:col-row:col`; the trailing position is the *last*
    // character - not the one Loc::end points past.
    CHECK(std::format("{}", Loc(src, Pos(4), Pos(9))) == "test.let:1:5-1:9");
    CHECK(std::format("{}", Loc(src, Pos(4), Pos(5))) == "test.let:1:5");
    CHECK(std::format("{}", Loc(src, Pos(4))) == "test.let:1:5");
    CHECK(std::format("{}", Loc(src, Pos(4), Pos(15))) == "test.let:1:5-2:4");

    // Offsets its Src cannot resolve degrade to raw offsets: better a raw range than a
    // plausible-looking 0:0.
    CHECK(std::format("{}", Loc(src, Pos(4), Pos(9999))) == "test.let@4-9999");
    CHECK(std::format("{}", Loc(src, Pos(9), Pos(4))) == "test.let@9-4");
    CHECK(std::format("{}", Loc(src, Pos(4), Pos())) == "test.let@4-<unknown position>");
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
    auto [src, _] = drv.src().add("test.let", "let x = 1;\nlet y = 2;\nlet z = 3;");

    CHECK(drv.num_errors() == 0);
    CHECK(drv.num_warnings() == 0);

    drv.err(Loc(src, Pos(4), Pos(5)), "expected '{}'", ';');
    drv.warn(Loc(src, Pos(15), Pos(16)), "unused variable '{}'", "x");
    drv.note(Loc(src, Pos(26), Pos(27)), "declared here");

    CHECK(drv.num_errors() == 1);
    CHECK(drv.num_warnings() == 1);
    CHECK(capture.oss.str()
          == "test.let:1:5: error: expected ';'\n"
             "test.let:2:5: warning: unused variable 'x'\n"
             "test.let:3:5: note: declared here\n");

    // Driver inherits SymPool.
    CHECK(drv.sym("foo") == drv.sym("foo"));
}
