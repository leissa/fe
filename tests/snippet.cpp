#include <sstream>

#include <doctest/doctest.h>
#include <fe/snippet.h>

using fe::Loc;
using fe::Pos;

namespace {

/// Renders a snippet with colors off so the text is predictable.
std::string render(Loc loc, std::string_view line) {
    auto mode = fe::term::mode();
    fe::term::set_mode(fe::term::Mode::Never);
    auto oss = std::ostringstream();
    fe::snippet(oss, loc, line);
    fe::term::set_mode(mode);
    return oss.str();
}

} // namespace

TEST_CASE("snippet") {
    std::filesystem::path path("test.let");

    SUBCASE("underlines the spanned columns") {
        CHECK(render(Loc(&path, {3, 9}, {3, 12}), "let n = id 3;")
              == "    3 | let n = id 3;\n"
                 "      |         ^^^^\n");
    }

    SUBCASE("a single column gets a single caret") {
        CHECK(render(Loc(&path, {1, 5}), "let n;")
              == "    1 | let n;\n"
                 "      |     ^\n");
    }

    SUBCASE("multi-byte code points before the span do not shift the carets") {
        // Pos::col counts code points, so `→` occupies one column despite being three bytes.
        CHECK(render(Loc(&path, {2, 16}), "axm foo: Nat → x;")
              == "    2 | axm foo: Nat → x;\n"
                 "      |                ^\n");
    }

    SUBCASE("multi-byte code points inside the span each get one caret") {
        CHECK(render(Loc(&path, {8, 9}, {8, 17}), "let n = «3; id» 3;")
              == "    8 | let n = «3; id» 3;\n"
                 "      |         ^^^^^^^^^\n");
    }

    SUBCASE("a tab is echoed so the carets stay aligned") {
        CHECK(render(Loc(&path, {1, 2}), "\tx")
              == "    1 | \tx\n"
                 "      | \t^\n");
    }

    SUBCASE("a span across rows underlines the rest of its first row") {
        CHECK(render(Loc(&path, {1, 5}, {9, 2}), "let n = (")
              == "    1 | let n = (\n"
                 "      |     ^^^^^\n");
    }

    SUBCASE("nothing to point at") {
        CHECK(render(Loc(&path, {1, 1}), "").empty());        // no line
        CHECK(render(Loc(&path, Pos(1)), "let n;").empty());  // no column
        CHECK(render(Loc(&path, {1, 99}), "let n;").empty()); // column past the line
        CHECK(render(Loc(), "let n;").empty());               // no location
    }

    SUBCASE("the gutter is configurable") {
        auto mode = fe::term::mode();
        fe::term::set_mode(fe::term::Mode::Never);
        auto oss = std::ostringstream();
        fe::snippet(oss, Loc(&path, {7, 1}, {7, 3}), "abc", fe::term::FG::Red, 1);
        fe::term::set_mode(mode);
        CHECK(oss.str()
              == "7 | abc\n"
                 "  | ^^^\n");
    }
}

TEST_CASE("Src") {
    auto path = std::filesystem::temp_directory_path() / "fe-snippet-test.let";
    {
        auto ofs = std::ofstream(path);
        ofs << "let a;\r\n" // CRLF must not leak into the line
               "let b;\n";
    }

    auto src = fe::Src();
    CHECK(src.line(Loc(&path, {1, 1})) == "let a;");
    CHECK(src.line(Loc(&path, {2, 1})) == "let b;");
    CHECK(src.line(Loc(&path, {2, 1})) == "let b;"); // memoized
    CHECK(src.line(Loc(&path, {3, 1})).empty());     // past the end
    CHECK(src.line(Loc({1, 1}, {1, 1})).empty());    // no path

    std::filesystem::remove(path);

    auto missing = std::filesystem::temp_directory_path() / "fe-snippet-test-absent.let";
    CHECK(fe::Src().line(Loc(&missing, {1, 1})).empty());
}
