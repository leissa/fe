#include <sstream>

#include <doctest/doctest.h>
#include <fe/driver.h>
#include <fe/error.h>
#include <fe/format.h>
#include <fe/loc.h>
#include <fe/snippet.h>
#include <fe/src.h>

using fe::Loc;
using fe::Pos;

TEST_CASE("Pos") {
    CHECK(!Pos());
    CHECK(Pos(0)); // 0 is the first byte of a file - not the invalid sentinel
    CHECK(Pos(3) < Pos(4));
    CHECK(Pos(3) + 4 == Pos(7));

    // Default operator<< from a FE_LIB build.
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

    SUBCASE("a trailing line terminator ends its row rather than opening a new one") {
        auto nl    = fe::Src("nl.let", "let x = 1;\n");
        auto nonl  = fe::Src("nonl.let", "let x = 1;");
        auto crlf  = fe::Src("crlf.let", "let x = 1;\r\n");
        auto blank = fe::Src("blank.let", "let x = 1;\n\n");
        auto empty = fe::Src("empty.let", "");

        CHECK(nl.num_rows() == 1);
        CHECK(nonl.num_rows() == 1);
        CHECK(crlf.num_rows() == 1);
        CHECK(empty.num_rows() == 1);

        // Whether the file ends with a terminator or not, its end spells the same position -
        // and the terminator itself is no column.
        CHECK(nl.rowcol(nl.end()) == std::pair(1u, 11u));
        CHECK(nonl.rowcol(nonl.end()) == std::pair(1u, 11u));
        CHECK(crlf.rowcol(crlf.end()) == std::pair(1u, 11u));
        CHECK(empty.rowcol(empty.end()) == std::pair(1u, 1u));

        // A row that really is empty is a row all the same: `blank` ends *its* row 2, so the
        // file end resolves there and not to a row 3.
        CHECK(blank.num_rows() == 2);
        CHECK(blank.rowcol(Pos(11)) == std::pair(2u, 1u));
        CHECK(blank.rowcol(blank.end()) == std::pair(2u, 1u));

        CHECK(nl.line(1) == "let x = 1;");
        CHECK(crlf.line(1) == "let x = 1;");
        CHECK(nl.line(2).empty());

        // The row a Loc names is the row its snippet underlines - see TEST_CASE("snippet").
        CHECK(std::format("{}", Loc(&nl, nl.end())) == "nl.let:1:11");
        CHECK(std::format("{}", Loc(&nonl, nonl.end())) == "nonl.let:1:11");
    }

    SUBCASE("a leading BOM is no column") {
        auto bom = fe::Src("bom.let", "\xEF\xBB\xBF"
                                      "let x = 1;\ny");
        CHECK(bom.col(Pos(3)) == 1); // where the lexer starts - see TEST_CASE("Lexer")
        CHECK(bom.col(Pos(4)) == 2);
        CHECK(bom.rowcol(Pos(14)) == std::pair(2u, 1u));
        CHECK(bom.line(1) == "let x = 1;");
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
    fe::Driver drv;
    auto [src, _] = drv.src().add("test.let", "let x = 1;\nlet y = 2;\nlet z = 3;");

    CHECK(drv.sym("foo") == drv.sym("foo")); // Driver inherits SymPool.
    CHECK(drv.src().lookup("test.let") == src);

    SUBCASE("Dbg interning") {
        auto dbg = fe::Dbg(Loc(src, Pos(4), Pos(5)), drv.sym("x"));
        auto key = drv.dbg(dbg);
        CHECK(key);
        CHECK(drv.dbg(key) == dbg);
        CHECK(drv.dbg(dbg) == key); // interned only once

        auto empty = drv.dbg(fe::Dbg());
        CHECK(!empty);
        CHECK(!drv.dbg(empty));
        CHECK(fe::DbgKey() == empty); // key 0 is always the empty Dbg
    }
}

TEST_CASE("Error") {
    // Disable colors so the diagnostic text is predictable.
    auto old_mode = fe::term::mode();
    fe::term::set_mode(fe::term::Mode::Never);

    fe::Driver drv;
    auto [src, _] = drv.src().add("test.let", "let x = 1;\nlet y = 2;\nlet z = 3;");

    auto err = fe::Error(drv);
    CHECK(err.ok());
    CHECK(err.empty());
    CHECK(err.num_errors() == 0);
    CHECK(err.num_warnings() == 0);

    err.error(Loc(src, Pos(4), Pos(5)), "expected '{}'", ';');
    err.warn(Loc(src, Pos(15), Pos(16)), "unused variable '{}'", "x");
    err.note(Loc(src, Pos(26), Pos(27)), "declared here");

    CHECK(!err.ok());
    CHECK(err.num_errors() == 1);
    CHECK(err.num_warnings() == 1);
    CHECK(err.num_notes() == 1);

    CHECK(std::format("{}", err)
          == "test.let:1:5: error: expected ';'\n"
             "    1 | let x = 1;\n"
             "      |     ^\n"
             "test.let:2:5: warning: unused variable 'x'\n"
             "    2 | let y = 2;\n"
             "      |     ^\n"
             "      test.let:3:5: note: declared here\n"
             "    3 | let z = 3;\n"
             "      |     ^\n"
             "1 error(s), 1 warning(s) encountered\n");

    SUBCASE("str renders the whole collection") { CHECK(err.str() == std::format("{}", err)); }

    SUBCASE("a note without a Loc stays a continuation line") {
        err.clear();
        err.error(Loc(src, Pos(4), Pos(5)), "expected '{}'", ';').note("inserted here");
        CHECK(err.msgs().front().notes.size() == 1);
        CHECK(std::format("{}", err)
              == "test.let:1:5: error: expected ';'\n"
                 "    1 | let x = 1;\n"
                 "      |     ^\n"
                 "      = note: inserted here\n"
                 "1 error(s) encountered\n");
    }

    SUBCASE("a note drops a Loc that points nowhere new") {
        err.clear();
        auto loc = Loc(src, Pos(4), Pos(5));
        err.error(loc, "oops");
        err.note(loc, "same spot"); // overlaps the primary Loc
        CHECK(err.num_notes() == 0);
        err.note(Loc(), "no Loc"); // degrades to a continuation instead of vanishing
        CHECK(err.num_notes() == 1);
        CHECK(!err.msgs().front().notes.front().loc);
        err.note(Loc(src, Pos(15), Pos(16)), "elsewhere");
        CHECK(err.num_notes() == 2);
        CHECK(err.msgs().front().notes.size() == 2);
    }

    SUBCASE("Diag lays the diagnostic out") {
        drv.diag.no_snippet = true;
        err.clear();
        err.error(Loc(src, Pos(4), Pos(5)), "just the header line");
        CHECK(std::format("{}", err)
              == "test.let:1:5: error: just the header line\n"
                 "1 error(s) encountered\n");

        drv.diag.no_snippet = false;
        drv.diag.gutter     = 2;
        CHECK(std::format("{}", err)
              == "test.let:1:5: error: just the header line\n"
                 " 1 | let x = 1;\n"
                 "   |     ^\n"
                 "1 error(s) encountered\n");
        drv.diag.gutter = 5;
    }

    SUBCASE("Diag::werror records a warning as an error") {
        drv.diag.werror = true;
        err.clear();
        err.warn(Loc(src, Pos(4), Pos(5)), "promoted");
        CHECK(err.num_errors() == 1);
        CHECK(err.num_warnings() == 0);
        CHECK(!err.ok());
        drv.diag.werror = false;
    }

    SUBCASE("Diag::max_errors drops the rest") {
        drv.diag.max_errors = 1;
        err.clear();
        err.error(Loc(src, Pos(4), Pos(5)), "kept").note("kept note");
        err.error(Loc(src, Pos(15), Pos(16)), "dropped").note("dropped note");
        CHECK(err.num_errors() == 1);
        CHECK(err.num_notes() == 1);
        CHECK(err.truncated());
        CHECK(std::format("{}", err).ends_with("1 error(s) encountered; further diagnostics dropped\n"));
        drv.diag.max_errors = 0;
    }

    SUBCASE("report streams and claims everything") {
        err.clear();
        err.warn(Loc(src, Pos(4), Pos(5)), "just a warning");
        std::ostringstream oss;
        CHECK(err.report(oss) == 0);
        CHECK(oss.str().ends_with("1 warning(s) encountered\n"));
        CHECK(err.empty());
        CHECK(err.report(oss) == 0); // nothing left to say
        CHECK(oss.str().ends_with("1 warning(s) encountered\n"));
    }

    SUBCASE("ack throws on errors and reports warnings") {
        err.clear();
        err.warn(Loc(src, Pos(4), Pos(5)), "just a warning");
        std::ostringstream oss;
        err.ack(oss);
        CHECK(oss.str().ends_with("1 warning(s) encountered\n"));
        CHECK(err.empty()); // ack claims the messages

        err.error(Loc(src, Pos(4), Pos(5)), "a real error");
        CHECK_THROWS_AS(err.ack(oss), fe::Error::Bail);
        CHECK(err.empty()); // ... and claims them here, too
        CHECK(err.num_errors() == 0);
    }

    SUBCASE("a Bail outlives the Src its Loc%s pointed into") {
        auto what = std::string();
        {
            fe::Driver tmp;
            auto [tmp_src, _] = tmp.src().add("gone.let", "let x = 1;\n");
            auto e            = fe::Error(tmp);
            e.error(Loc(tmp_src, Pos(4), Pos(5)), "vanishing");
            try {
                e.bail();
            } catch (const fe::Error::Bail& bail) {
                CHECK(bail.num_errors() == 1);
                what = bail.what();
            }
        }
        CHECK(what.starts_with("gone.let:1:5: error: vanishing\n"));
        CHECK(what.ends_with("1 error(s) encountered\n"));
    }

    SUBCASE("a chain on a temporary bails out in one expression") {
        try {
            fe::Error(drv)
                .error(Loc(src, Pos(4), Pos(5)), "boom")
                .note("why")
                .note(Loc(src, Pos(15), Pos(16)), "and there")
                .bail();
        } catch (const fe::Error::Bail& bail) {
            auto what = std::string(bail.what());
            CHECK(bail.num_errors() == 1);
            CHECK(what.find("= note: why") != std::string::npos);
            CHECK(what.find("note: and there") != std::string::npos);
        }
    }

    SUBCASE("Driver::render may render a message twice") {
        auto calls   = 0;
        auto retry   = fe::Driver();
        retry.render = [&](const std::function<std::string()>& fmt) {
            ++calls;
            fmt();
            return fmt();
        };

        auto e = fe::Error(retry);
        e.error(Loc(), "hi");
        CHECK(calls == 1);
        CHECK(e.msgs().front().str == "hi");
    }

    fe::term::set_mode(old_mode);
}

TEST_CASE("snippet") {
    auto mode = fe::term::mode();
    fe::term::set_mode(fe::term::Mode::Never);

    auto src = fe::Src("test.let", "let x = 1;\nlet y = 2;\nlet z = 3;\nlet w = 4;\n");
    auto str = [&](Loc loc, uint32_t max_rows) {
        std::ostringstream oss;
        oss << fe::Snippet{loc, fe::term::FG::Red, 5, max_rows};
        return oss.str();
    };

    CHECK(str(Loc(), 0).empty());               // no Src to resolve against
    CHECK(str(Loc(Pos(0), Pos(3)), 0).empty()); // ditto

    CHECK(str(Loc(&src, Pos(4), Pos(5)), 0)
          == "    1 | let x = 1;\n"
             "      |     ^\n");

    // An empty Loc past the last column - a missing token - still gets its row and a caret.
    CHECK(str(Loc(&src, Pos(10), Pos(10)), 0)
          == "    1 | let x = 1;\n"
             "      |           ^\n");

    // The trailing newline opens a row 5 that the source does not actually contain:
    // a Loc there falls back to the end of row 4.
    CHECK(str(Loc(&src, Pos(44), Pos(44)), 0)
          == "    4 | let w = 4;\n"
             "      |           ^\n");

    // A row that really is empty is a row all the same and keeps its caret.
    auto src2 = fe::Src("test2.let", "let x = 1;\n\n");
    CHECK(str(Loc(&src2, Pos(11), Pos(11)), 0)
          == "    2 | \n"
             "      | ^\n");
    CHECK(str(Loc(&src2, Pos(12), Pos(12)), 0) == str(Loc(&src2, Pos(11), Pos(11)), 0));

    // A Loc spanning more rows than `max_rows` elides its middle.
    CHECK(str(Loc(&src, Pos(4), Pos(37)), 2)
          == "    1 | let x = 1;\n"
             "      |     ^^^^^^\n"
             "  ... |\n"
             "    4 | let w = 4;\n"
             "      | ^^^^\n");

    fe::term::set_mode(mode);
}
