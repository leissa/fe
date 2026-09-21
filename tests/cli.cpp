#include <format>
#include <sstream>

#include <doctest/doctest.h>
#include <fe/cli.h>
#include <fe/term.h>

using namespace std::literals;

namespace {

std::optional<std::string> parse(fe::Cli& cli, std::initializer_list<const char*> args) {
    auto argv = std::vector<const char*>(args);
    return cli.parse(int(argv.size()), argv.data());
}

} // namespace

TEST_CASE("cli flags") {
    bool a = false, b = false, c = false;
    int n    = 0;
    auto inc = [&](bool) { ++n; };

    auto cli = fe::Cli("t").opt(a, "-a").opt(b, "-b", "--bee").opt(c, "-c").opt(inc, "", "-V", "--verbose");

    SUBCASE("long and short") {
        CHECK(!parse(cli, {"t", "-a", "--bee"}));
        CHECK(a);
        CHECK(b);
        CHECK(!c);
    }

    SUBCASE("clustered") {
        CHECK(!parse(cli, {"t", "-ac", "-VVV"}));
        CHECK(a);
        CHECK(!b);
        CHECK(c);
        CHECK(n == 3);
    }

    SUBCASE("a flag takes no value") { CHECK(parse(cli, {"t", "--bee=1"}) == "option '--bee' does not take a value"); }

    SUBCASE("unknown") { CHECK(parse(cli, {"t", "--nope"}) == "unknown option '--nope'"); }
}

TEST_CASE("cli values") {
    std::string out;
    uint32_t num = 5;
    std::vector<std::string> plugins;
    std::vector<uint32_t> gids;
    std::string mode;
    auto set_mode = [&](const std::string& s) { mode = s; };

    auto cli = fe::Cli("t")
                   .opt(out, "file", "-o", "--output")
                   .opt(num, "num", "-n", "--num")
                   .opt(plugins, "plugin", "-p")
                   .opt(gids, "gid", "-g")
                   .opt(set_mode, "mode", "", "--mode");

    SUBCASE("separate, attached, and =") {
        CHECK(!parse(cli, {"t", "-o", "x.txt", "-n7", "--mode=tree", "-p", "a", "-p", "b", "-g", "1", "-g", "2"}));
        CHECK(out == "x.txt");
        CHECK(num == 7);
        CHECK(mode == "tree");
        CHECK(plugins == std::vector{"a"s, "b"s});
        CHECK(gids == std::vector<uint32_t>{1, 2});
    }

    SUBCASE("a value may look like an option") {
        CHECK(!parse(cli, {"t", "-o", "-"}));
        CHECK(out == "-");
    }

    SUBCASE("missing value") { CHECK(parse(cli, {"t", "--output"}) == "option '--output' requires a value <file>"); }

    SUBCASE("a handler may reject a value") {
        std::string mode2;
        auto pick = [&](const std::string& t) -> std::string {
            if (t != "tree" && t != "flat") return std::format("'{}' is not a mode", t);
            mode2 = t;
            return {};
        };
        auto cli2 = fe::Cli("t").opt(pick, "mode", "", "--mode");
        CHECK(!parse(cli2, {"t", "--mode", "tree"}));
        CHECK(mode2 == "tree");

        auto cli3 = fe::Cli("t").opt(pick, "mode", "", "--mode");
        CHECK(parse(cli3, {"t", "--mode", "nope"}) == "option '--mode': 'nope' is not a mode");
    }

    SUBCASE("not a number") { CHECK(parse(cli, {"t", "--num", "3x"}) == "option '--num': '3x' is not a number"); }
}

TEST_CASE("cli args") {
    std::string in;
    std::vector<std::string> rest;

    SUBCASE("one positional") {
        bool flag = false;
        auto cli  = fe::Cli("t").opt(flag, "-f").arg(in, "file");
        CHECK(!parse(cli, {"t", "-f", "in.txt"}));
        CHECK(in == "in.txt");
        CHECK(flag);
    }

    SUBCASE("too many positionals") {
        auto cli = fe::Cli("t").arg(in, "file");
        CHECK(parse(cli, {"t", "a", "b"}) == "unexpected argument 'b'");
    }

    SUBCASE("-- ends option processing") {
        bool flag = false;
        auto cli  = fe::Cli("t").opt(flag, "-f").arg(in, "file");
        CHECK(!parse(cli, {"t", "--", "-f"}));
        CHECK(in == "-f");
        CHECK(!flag);
    }

    SUBCASE("a rejected positional is an argument, not an option") {
        int num  = 0;
        auto cli = fe::Cli("t").arg(num, "num");
        CHECK(parse(cli, {"t", "3x"}) == "argument 'num': '3x' is not a number");
    }

    SUBCASE("a vector soaks up the rest") {
        auto cli = fe::Cli("t").arg(in, "first").arg(rest, "more");
        CHECK(!parse(cli, {"t", "a", "b", "c"}));
        CHECK(in == "a");
        CHECK(rest == std::vector{"b"s, "c"s});
    }
}

TEST_CASE("cli cardinality") {
    int n    = 0;
    auto inc = [&](bool) { ++n; };
    auto cli = fe::Cli("t").opt(inc, "", "-V").cardinality(1, 2);

    CHECK(parse(cli, {"t"}) == "missing option '-V'");

    auto cli2 = fe::Cli("t").opt(inc, "", "-V").cardinality(1, 2);
    CHECK(parse(cli2, {"t", "-VVV"}) == "option '-V' must not occur more than 2 times");

    std::string in;
    auto cli3 = fe::Cli("t").arg(in, "file").cardinality(1, 1);
    CHECK(parse(cli3, {"t"}) == "missing argument 'file'");

    std::vector<std::string> rest;
    auto cli4 = fe::Cli("t").arg(rest, "file").cardinality(0, 2);
    CHECK(parse(cli4, {"t", "a", "b", "c"}) == "argument 'file' must not occur more than 2 times");
}

TEST_CASE("cli section") {
    bool flag = false;
    auto cli  = fe::Cli("t").opt(flag, "-f", "--flag", "A flag.");
    cli.section("-X ll:<arg>", "Argument",
                {
                    {"o=<file>, output=<file>",                                    "Write the LLVM IR to `<file>`."},
                    {               "rt=embed", "Passed to `--cmdline`; unlike a bare --cmdline outside backticks."}
    });

    SUBCASE("terminal") {
        auto _ = fe::term::ScopedMode(fe::term::Mode::Never);
        std::ostringstream oss;
        oss << cli;
        CHECK(oss.str() == R"(Usage: t [options]

Options:
  -f, --flag               A flag.

-X ll:<arg>:
  o=<file>, output=<file>  Write the LLVM IR to `<file>`.
  rt=embed                 Passed to `--cmdline`; unlike a bare --cmdline
                           outside backticks.
)");
    }

    SUBCASE("a section without rows is a bare header") {
        auto cli2 = fe::Cli("t").opt(flag, "-f", "", "A flag.");
        cli2.section("Plugin Arguments");
        cli2.section("-X ll:<arg>", "Argument",
                     {
                         {"o=<file>", "Where to write."}
        });

        std::ostringstream oss;
        cli2.markdown(oss);
        auto md = oss.str();
        CHECK(md.contains("\n## Plugin Arguments\n\n### -X ll:&lt;arg&gt;\n"));

        auto _ = fe::term::ScopedMode(fe::term::Mode::Never);
        std::ostringstream term;
        term << cli2;
        CHECK(term.str().contains("\nPlugin Arguments:\n\n-X ll:<arg>:\n"));
    }

    SUBCASE("markdown escapes outside code spans only") {
        std::ostringstream oss;
        cli.markdown(oss);
        auto md = oss.str();
        CHECK(md.contains("### -X ll:&lt;arg&gt;\n\n| Argument | Description |"));
        CHECK(md.contains("Passed to `--cmdline`; unlike a bare \\--cmdline outside backticks."));
    }
}

TEST_CASE("cli help") {
    bool show_help = false, verbose = false;
    uint32_t gutter = 5;
    std::string in, out;

    auto cli = fe::Cli("t", "Does things.")
                   .help(show_help)
                   .opt(verbose, "-V", "--verbose", "Be verbose.")
                   .grp("Output")
                   .opt(out, "file", "-o", "--output", "Where to write the result.")
                   .opt(gutter, "width", "", "--gutter", "Column width.")
                   .arg(in, "file", "Input file.")
                   .epilog("Bye.");

    CHECK(!parse(cli, {"t", "--help"}));
    CHECK(show_help);

    SUBCASE("the help flag may be renamed") {
        bool h2   = false;
        auto cli2 = fe::Cli("t").help(h2, "-?", "--usage");
        CHECK(!parse(cli2, {"t", "-?"}));
        CHECK(h2);

        auto cli3 = fe::Cli("t").help(h2, "-?", "--usage");
        CHECK(parse(cli3, {"t", "--help"}) == "unknown option '--help'");

        auto _ = fe::term::ScopedMode(fe::term::Mode::Never);
        std::ostringstream oss;
        oss << cli2;
        CHECK(oss.str().contains("-?, --usage  Display this help and exit."));
    }

    SUBCASE("terminal") {
        auto _ = fe::term::ScopedMode(fe::term::Mode::Never);
        std::ostringstream oss;
        oss << cli;
        CHECK(oss.str() == R"(Usage: t [options] <file>

Does things.

Arguments:
  <file>                Input file.

Options:
  -h, --help            Display this help and exit.
  -V, --verbose         Be verbose.

Output:
  -o, --output <file>   Where to write the result.
      --gutter <width>  Column width. [default: `5`]

Bye.
)");
    }

    SUBCASE("markdown") {
        std::ostringstream oss;
        cli.markdown(oss);
        CHECK(oss.str() == R"(```
t [options] <file>
```

Does things.

### Arguments

| Argument | Description |
| --- | --- |
| `<file>` | Input file. |

### Options

| Option | Description |
| --- | --- |
| `-h, --help` | Display this help and exit. |
| `-V, --verbose` | Be verbose. |

### Output

| Option | Description |
| --- | --- |
| `-o, --output <file>` | Where to write the result. |
| `--gutter <width>` | Column width. [default: `5`] |

Bye.
)");
    }
}

TEST_CASE("cli citations") {
    auto _ = fe::term::ScopedMode(fe::term::Mode::Never);

    SUBCASE("a default value is data, not markup") {
        auto out = std::string("C:\\tmp\\");
        auto cli = fe::Cli("t").opt(out, "file", "-o", "--output", "Where to write.");

        std::ostringstream oss;
        oss << cli;
        CHECK(oss.str().contains("[default: `C:\\tmp\\`]"));
    }

    SUBCASE("help and markdown read the same escapes") {
        auto cli = fe::Cli("t", "\\` then `a<b` and a|b -- end");

        std::ostringstream term;
        term << cli;
        CHECK(term.str().contains("` then `a<b` and a|b -- end"));

        std::ostringstream md;
        cli.markdown(md);
        CHECK(md.str().contains("` then `a<b` and a\\|b \\-- end"));
    }
}
