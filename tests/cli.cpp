#include <format>
#include <sstream>

#include <doctest/doctest.h>
#include <fe/cli.h>

using namespace std::literals;

namespace {

struct Args {
    Args(std::initializer_list<const char*> args)
        : argv(args) {}

    int argc() const { return int(argv.size()); }
    const char* const* data() const { return argv.data(); }

    std::vector<const char*> argv;
};

} // namespace

TEST_CASE("cli flags") {
    bool a = false, b = false, c = false;
    int n    = 0;
    auto inc = [&](bool) { ++n; };

    auto cli = fe::cli::Cli("t") | fe::cli::opt(a)["-a"] | fe::cli::opt(b)["-b"]["--bee"] | fe::cli::opt(c)["-c"]
             | fe::cli::opt(inc)["-V"]["--verbose"];

    SUBCASE("long and short") {
        auto args = Args{"t", "-a", "--bee"};
        CHECK(cli.parse(args.argc(), args.data()));
        CHECK(a);
        CHECK(b);
        CHECK(!c);
    }

    SUBCASE("clustered") {
        auto args = Args{"t", "-ac", "-VVV"};
        CHECK(cli.parse(args.argc(), args.data()));
        CHECK(a);
        CHECK(!b);
        CHECK(c);
        CHECK(n == 3);
    }

    SUBCASE("a flag takes no value") {
        auto args = Args{"t", "--bee=1"};
        auto res  = cli.parse(args.argc(), args.data());
        CHECK(!res);
        CHECK(res.message() == "option '--bee' does not take a value");
    }

    SUBCASE("unknown") {
        auto args = Args{"t", "--nope"};
        auto res  = cli.parse(args.argc(), args.data());
        CHECK(!res);
        CHECK(res.message() == "unknown option '--nope'");
    }
}

TEST_CASE("cli values") {
    std::string out;
    uint32_t num = 5;
    std::vector<std::string> plugins;
    std::vector<uint32_t> gids;
    std::string mode;
    auto set_mode = [&](const std::string& s) { mode = s; };

    auto cli = fe::cli::Cli("t") | fe::cli::opt(out, "file")["-o"]["--output"] | fe::cli::opt(num, "num")["-n"]["--num"]
             | fe::cli::opt(plugins, "plugin")["-p"] | fe::cli::opt(gids, "gid")["-g"]
             | fe::cli::opt(set_mode, "mode")["--mode"];

    SUBCASE("separate, attached, and =") {
        auto args = Args{"t", "-o", "x.txt", "-n7", "--mode=tree", "-p", "a", "-p", "b", "-g", "1", "-g", "2"};
        CHECK(cli.parse(args.argc(), args.data()));
        CHECK(out == "x.txt");
        CHECK(num == 7);
        CHECK(mode == "tree");
        CHECK(plugins == std::vector{"a"s, "b"s});
        CHECK(gids == std::vector<uint32_t>{1, 2});
    }

    SUBCASE("a value may look like an option") {
        auto args = Args{"t", "-o", "-"};
        CHECK(cli.parse(args.argc(), args.data()));
        CHECK(out == "-");
    }

    SUBCASE("missing value") {
        auto args = Args{"t", "--output"};
        auto res  = cli.parse(args.argc(), args.data());
        CHECK(!res);
        CHECK(res.message() == "option '--output' requires a value <file>");
    }

    SUBCASE("a handler may reject a value") {
        std::string mode2;
        auto pick = [&](const std::string& t) -> std::string {
            if (t != "tree" && t != "flat") return std::format("'{}' is not a mode", t);
            mode2 = t;
            return {};
        };
        auto cli2 = fe::cli::Cli("t") | fe::cli::opt(pick, "mode")["--mode"];
        auto ok   = Args{"t", "--mode", "tree"};
        auto bad  = Args{"t", "--mode", "nope"};
        CHECK(cli2.parse(ok.argc(), ok.data()));
        CHECK(mode2 == "tree");

        auto cli3 = fe::cli::Cli("t") | fe::cli::opt(pick, "mode")["--mode"];
        auto res  = cli3.parse(bad.argc(), bad.data());
        CHECK(!res);
        CHECK(res.message() == "option '--mode': 'nope' is not a mode");
    }

    SUBCASE("not a number") {
        auto args = Args{"t", "--num", "3x"};
        auto res  = cli.parse(args.argc(), args.data());
        CHECK(!res);
        CHECK(res.message() == "option '--num': '3x' is not a number");
    }
}

TEST_CASE("cli args") {
    std::string in;
    std::vector<std::string> rest;

    SUBCASE("one positional") {
        bool flag = false;
        auto cli  = fe::cli::Cli("t") | fe::cli::opt(flag)["-f"] | fe::cli::arg(in, "file");
        auto args = Args{"t", "-f", "in.txt"};
        CHECK(cli.parse(args.argc(), args.data()));
        CHECK(in == "in.txt");
        CHECK(flag);
    }

    SUBCASE("too many positionals") {
        auto cli  = fe::cli::Cli("t") | fe::cli::arg(in, "file");
        auto args = Args{"t", "a", "b"};
        auto res  = cli.parse(args.argc(), args.data());
        CHECK(!res);
        CHECK(res.message() == "unexpected argument 'b'");
    }

    SUBCASE("-- ends option processing") {
        bool flag = false;
        auto cli  = fe::cli::Cli("t") | fe::cli::opt(flag)["-f"] | fe::cli::arg(in, "file");
        auto args = Args{"t", "--", "-f"};
        CHECK(cli.parse(args.argc(), args.data()));
        CHECK(in == "-f");
        CHECK(!flag);
    }

    SUBCASE("a vector soaks up the rest") {
        auto cli  = fe::cli::Cli("t") | fe::cli::arg(in, "first") | fe::cli::arg(rest, "more");
        auto args = Args{"t", "a", "b", "c"};
        CHECK(cli.parse(args.argc(), args.data()));
        CHECK(in == "a");
        CHECK(rest == std::vector{"b"s, "c"s});
    }
}

TEST_CASE("cli cardinality") {
    int n     = 0;
    auto inc  = [&](bool) { ++n; };
    auto cli  = fe::cli::Cli("t") | fe::cli::opt(inc)["-V"].cardinality(1, 2);
    auto none = Args{"t"};
    auto many = Args{"t", "-VVV"};

    auto res = cli.parse(none.argc(), none.data());
    CHECK(!res);
    CHECK(res.message() == "missing option '-V'");

    auto cli2 = fe::cli::Cli("t") | fe::cli::opt(inc)["-V"].cardinality(1, 2);
    auto res2 = cli2.parse(many.argc(), many.data());
    CHECK(!res2);
    CHECK(res2.message() == "option '-V' must not occur more than 2 times");
}

TEST_CASE("cli section") {
    bool flag = false;
    auto cli  = fe::cli::Cli("t") | fe::cli::opt(flag)["-f"]["--flag"]("A flag.");
    cli.section("-X ll:<arg>", "Argument",
                {
                    {"o=<file>, output=<file>",                                    "Write the LLVM IR to `<file>`."},
                    {               "rt=embed", "Passed to `--cmdline`; unlike a bare --cmdline outside backticks."}
    });

    SUBCASE("terminal") {
        auto guard = fe::term::ScopedMode(fe::term::Mode::Never);
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

    SUBCASE("markdown escapes outside code spans only") {
        std::ostringstream oss;
        cli.md(oss);
        auto md = oss.str();
        CHECK(md.contains("### -X ll:&lt;arg&gt;\n\n| Argument | Description |"));
        CHECK(md.contains("Passed to `--cmdline`; unlike a bare \\--cmdline outside backticks."));
    }
}

TEST_CASE("cli help") {
    bool show_help = false, verbose = false;
    uint32_t gutter = 5;
    std::string in, out;

    auto cli = fe::cli::Cli("t", "Does things.") | fe::cli::help(show_help)
             | fe::cli::opt(verbose)["-V"]["--verbose"]("Be verbose.") | fe::cli::group("Output")
             | fe::cli::opt(out, "file")["-o"]["--output"]("Where to write the result.")
             | fe::cli::opt(gutter, "width")["--gutter"]("Column width.") | fe::cli::arg(in, "file")("Input file.");
    cli.epilog("Bye.");

    auto args = Args{"t", "--help"};
    CHECK(cli.parse(args.argc(), args.data()));
    CHECK(show_help);

    SUBCASE("terminal") {
        auto guard = fe::term::ScopedMode(fe::term::Mode::Never);
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
      --gutter <width>  Column width. [default: 5]

Bye.
)");
    }

    SUBCASE("markdown") {
        std::ostringstream oss;
        cli.md(oss);
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
