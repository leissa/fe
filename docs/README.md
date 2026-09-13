# FE

[![Stars](https://img.shields.io/github/stars/leissa/fe)](https://github.com/leissa/fe/stargazers)
[![Forks](https://img.shields.io/github/forks/leissa/fe)](https://github.com/leissa/fe/fork)

[![GitHub Release](https://img.shields.io/github/v/release/leissa/fe?style=flat-square&logo=starship&color=blue&label=Release)](https://github.com/leissa/fe/releases)
[![Documentation](https://img.shields.io/badge/Docs-main-blue?style=flat-square&logo=gitbook&logoColor=white)](https://leissa.github.io/fe)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?style=flat-square&logo=cplusplus)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)
[![License](https://img.shields.io/github/license/leissa/fe?style=flat-square&color=blue&logo=opensourceinitiative&logoColor=white&label=License)](https://github.com/leissa/fe/blob/main/LICENSE)

[![Doxygen](https://img.shields.io/github/actions/workflow/status/leissa/fe/doxygen.yml?style=flat-square&logo=doxygen&logoSize=auto&label=&labelColor=555&branch=main)](https://github.com/leissa/fe/actions/workflows/doxygen.yml?query=branch%3Amain)
[![Linux](https://img.shields.io/github/actions/workflow/status/leissa/fe/linux.yml?style=flat-square&logo=linux&label=Linux&logoColor=white&branch=main)](https://github.com/leissa/fe/actions/workflows/linux.yml?query=branch%3Amain)
[![macOS](https://img.shields.io/github/actions/workflow/status/leissa/fe/macos.yml?style=flat-square&logo=apple&label=macOS&branch=main)](https://github.com/leissa/fe/actions/workflows/macos.yml?query=branch%3Amain)
[![Windows](https://img.shields.io/github/actions/workflow/status/leissa/fe/windows.yml?style=flat-square&label=⊞%20Windows&branch=main)](https://github.com/leissa/fe/actions/workflows/windows.yml?query=branch%3Amain)

[TOC]

**FE** is a C++23 toolkit for building handwritten compiler and interpreter frontends.
It is one library target - `fe` - that you add as a subdirectory and link.

Rather than generating lexers or parsers for you, FE focuses on the infrastructure that every frontend needs anyway: source locations, diagnostics, interning, parsing support, command-line handling, and efficient memory management.
The goal is simple: keep handwritten frontends lightweight, explicit, and pleasant to maintain.

## 💡 Why FE?

FE is a good fit if you want to build:

- a small programming language or DSL,
- a hand-written recursive-descent parser,
- a lexer with precise UTF-8-aware source tracking,
- a frontend with high-quality diagnostics,
- a prototype compiler or interpreter that should stay easy to evolve.

It is especially useful when you want the flexibility of handwritten code without repeatedly rebuilding the same frontend infrastructure from scratch.

### How much code is that?

[**Let**](https://github.com/leissa/let) is a complete little language: lexer, parser, AST, arena-allocated nodes, evaluator, printer, CLI, and a golden-file test suite.
`sloccount src include` says 598 lines.

| | SLOC |
| ------------------------------------ | ---: |
| lexer + parser                       |  203 |
| token type: tag list and precedences |  128 |
| AST, evaluator, printer              |  201 |
| driver + CLI                         |   66 |

A `.l`/`.y` pair for that grammar would not come out much shorter than those 203 lines - and Bison would additionally check the grammar for conflicts, which recursive descent never will.
What a generator does *not* write for you is the other 395: command-line parsing, the AST, the arena, the interning, the evaluator, the printer.
Nor does it write the diagnostics, and that is where the difference actually shows up.

### Diagnostics you did not write

Those 203 lines already produce this.
The error line and its snippet come out of `expect`; the note that points back at the `(` is a three-line `syntax_err` override plus one `fe::Restore` to remember which `(` it was:

```
test/error/unclosed_paren.let:1:13: error: expected `)`, got `;` while parsing parenthesized expression
    1 | print (1 + 2;
      |             ^
      test/error/unclosed_paren.let:1:7: note: unmatched `(` opened here
    1 | print (1 + 2;
      |       ^
1 error(s) encountered
```

An *anchor* is a token an enclosing context is still waiting for, so a nested parser bails out instead of swallowing it.
That is what lets a stray `)` be a message the parser recovers from - three times in one run - instead of the end of the parse:

```
test/error/stray_paren.let:1:12: error: ignoring unmatched `)` while parsing right-hand side of binary expression
    1 | print 3 + 4) + 5;
      |            ^
test/error/stray_paren.let:2:14: error: ignoring unmatched `)` while parsing print-statement
    2 | print (1 + 2)) * 2;
      |              ^
test/error/stray_paren.let:3:7: error: ignoring unmatched `)` while parsing print-statement
    3 | print ) + 5;
      |       ^
3 error(s) encountered
```

Every message above is FE's own wording, summary line included; the only text Let contributes is that one note.
A generator hands you the parse and `yyerror("syntax error")` - the snippets, the notes, the recovery, and the `--max-errors` truncation are yours to build.

### No generated code to debug

There is no code generation step, so there is no generated code to debug and no build-time dependency on a tool.
`parse_expr` is a function that says what it does, in the language the rest of your compiler is written in.

## ✨ Features

Handwritten frontends are often the right choice when you want full control over syntax, diagnostics, recovery, and architecture. FE embraces that style.

It provides a compact set of reusable, well-integrated components:

### Building Blocks

#### Core

- `fe::Driver` for shared frontend state: the SymPool, the SrcMap, the interned `Dbg`s, the `Diag` that lays a diagnostic out, and the `Error` everything reports into.
  Global variables in all but name - which is the point: they live in one object you own and pass around, not in the global namespace.
- `fe::Arena` for fast arena allocation and arena-backed ownership.
- `fe::Sym` and `fe::SymPool` for string interning and cheap identifier comparison.

#### Lexing & Parsing

- `fe::Lexer<K, S>` for UTF-8-aware lexing with lookahead and token text accumulation.
- `fe::Parser<Tok, Tag, K, S>` for recursive-descent-style parsing with token lookahead, span tracking, and anchor-based error recovery.
  Both blueprints ask their child for a `fe::Driver& driver()` and report their default diagnostics into its `Error`.
- `fe::utf8` for lightweight UTF-8 handling.

#### Diagnostics

- `fe::Pos` and `fe::Loc` for source positions and source spans.
- `fe::Src` and `fe::SrcMap` for owning source text and resolving a position back to `path:row:col`.
- `fe::Dbg` for the `Loc`/`Sym` pair every named entity drags along, interned in the `Driver` as a `DbgKey`.
- `fe::Error` for collecting diagnostics - errors, warnings and their notes - and rendering each with its source snippet; `Error::ack` throws what it collected as a self-contained `Error::Bail`.
  The `Driver` owns the one everything reports into (`Driver::error`); `Error::{e,w,n}` open an error, a warning, and a note, in the spirit of `Log::e` and friends.
- `fe::Diag` for how a diagnostic lays out: `Diag::loc_style` (a `Loc::Style`), `Diag::no_snippet`, and friends cover the usual adjustments, and one virtual per piece (`loc`, `header`, `snippet`, `note`, `summary`, `render`) covers the rest.
  Derive and `Driver::diag(std::make_unique<MyDiag>())` to lay one out entirely your own way.
  `Diag::render` resolves the citation markup to plain text; override it to read that structure yourself.
- `fe::Snippet` for the underlined source excerpt `fe::Diag` puts below a diagnostic.
- `fe::Log` for leveled logging with acronym, color, and origin prefix.
    - `Log::{e,w,i,v}` - plus `Log::{d,t}`, which vaporize in a `Release` build - point at their call site via `std::source_location`; no macros involved.
    - They take a `cite_string` like `fe::Error` does, so a `` `citation` `` is colored and an argument is data.
- `fe::term` for lightweight terminal colors - and for the `` `citation` `` convention every FE message is written in; see [Citations](#citations).
  `term::mode` and `term::auto_detached` are one setting per process, so a shared library loaded via `fe::dl` follows the host instead of starting over from the defaults.

#### Command Line

- `fe::Cli` for parsing `argc`/`argv` of a single command: chain `Cli::opt`/`Cli::arg` and bind each switch to a variable of yours - a `bool`, a `std::string`, an integral, a `std::vector` of those, or a callable.
  It understands `--name value`, `--name=value`, `-n value`, `-nvalue`, clustered short flags, and `--`.
- `Cli::help` lays those switches out for a terminal - grouped into sections by `Cli::grp`, wrapped to the terminal width, and colored via `fe::term`.
  `Cli::markdown` renders the very same information as Markdown tables, so `--help` and the manual cannot drift apart.
- `Cli::section` for titled `term`/description rows that aren't options - `ENVIRONMENT`, plugin arguments, and the like - rendered below the options in both backends.

#### Data Structures

- `fe::Span`/`fe::View` and `fe::Vector` for spans with structured binding and small-buffer vectors.
- `fe::Bitset` for a dynamically growing bit set that keeps small sets inline and only allocates once they grow.
- `fe::XTrie` for interned, immutable sets - an [IndexedTrie](https://dl.acm.org/doi/10.1145/3808286) that is space-efficient and answers intersection tests fast.
- `fe::BFSWorklist`/`fe::DFSWorklist` for worklist traversals that visit each element at most once.
- Optional `FE_ABSL` support for [Abseil](https://abseil.io/) hash containers.
- `fe/container.h` for some helpers.

#### Algorithms

- `fe::hash` and friends for cheap, `constexpr` hash mixing/combining.
- `fe::Restore` for RAII save/restore of a variable - or of anything a getter/setter pair reaches, like `term::ScopedMode` - across a scope.
- `fe/algo.h` for some helpers.

#### System

- `fe::Profiler` for nested wall-clock spans reported as a flat table, a tree, or Chrome Trace JSON.
- `fe::dl` for loading dynamic libraries.
- `fe::sys` for locating/running external commands.

## 🚀 Quick Start

The easiest way to get going is through [**Let**](https://github.com/leissa/let).

You can either:

- 📦 create a [new repository from the Let template](https://github.com/new?template_owner=leissa&template_name=let), or
- 🍴 [fork Let directly](https://github.com/leissa/let/fork).

That gives you a concrete, working example of how FE is intended to be used in practice.

### CMake

Add FE as a subdirectory and link the `fe` target:

```cmake
add_subdirectory(submodules/fe)
target_link_libraries(my_compiler PRIVATE fe)
```

Set any of the options below *before* adding the subdirectory:

```cmake
set(FE_ABSL ON) # use Abseil-backed hash containers
add_subdirectory(submodules/fe)
target_link_libraries(my_compiler PRIVATE fe)
```

`fe` is an `OBJECT` library, so its objects land inside whichever target links it directly, while everything downstream just gets the headers.
Link it into exactly one shared library of yours and every other consumer resolves its symbols there instead of carrying a copy.
On Windows that shared library has to export them, which CMake cannot infer: compile everything that goes into it with `fe_EXPORTS`, or `FE_STATIC_DEFINE` if there is no shared library in play.

```cmake
set_target_properties(my_lib PROPERTIES DEFINE_SYMBOL fe_EXPORTS)
```

## 🧭 Typical Workflow

A typical FE-based frontend looks roughly like this:

1. Define a token type exposing `tag()` and `loc()`.
2. Implement your lexer by deriving from `fe::Lexer<K, S>`.
3. Implement your parser by deriving from `fe::Parser<Tok, Tag, K, S>`.
4. Use `fe::Driver` to centralize shared state; its `fe::Error` collects the diagnostics.
5. Register each source file with `fe::Driver::src()` so a `fe::Loc` can resolve itself to `path:row:col`.
6. Thread `fe::Loc` through tokens and AST nodes for precise error reporting.
7. Use `fe::Arena` and symbol interning where allocation cost and identifier handling matter.

If you want a concrete model to copy from, start with [`tests/lexer.cpp`](../tests/lexer.cpp).

## 💬 Writing a Diagnostic

Everything reports into the one `fe::Error` the `Driver` owns, and one diagnostic is one chained expression:

```cpp
error().e(tok.loc(), "expected `)`, got `{}` while parsing {}", tok, Cite(ctxt))
       .n(open.loc(), "unmatched `(` opened here");
```

`Error::e` opens an error and `Error::w` a warning; `Error::n` hangs a note off whichever came last.
A note *with* a `Loc` reads as a diagnostic of its own - header line plus snippet - and is dropped when that `Loc` merely repeats the primary one; a note without one has nowhere else to point and renders as a `= note:` continuation.
Nothing throws along the way: `Error::bail` throws what has accumulated as an `Error::Bail`, and `Error::ack` at the end of the run does that only if an error was among it and otherwise just reports the warnings.

### Citations {#citations}

A message spells the things it talks about in backticks, and `fe::CodeDiag` colors what they enclose - or keeps the backticks when there is no color to spend:

```
test.let:1:11: error: identifier `+` not found
```

#### The Markup Language {#markup}

The whole language is: a `` ` `` opens a citation and the next one closes it.

```
message  ::= piece*
piece    ::= text | citation
citation ::= '`' text '`'
text     ::= (char | '\`' | '\\')*   -- any char except an unescaped '`'
```

- Citations are paired left to right and do **not** nest: in `` `a` `b` `` the 1st and 2nd backtick delimit one citation, the 3rd and 4th the next.
- A backtick with no partner left in the message is not markup and renders as itself: the rest of the message comes out uncited instead of being swallowed.
- `` \` `` is a literal backtick and `\\` a literal backslash; both render as the single character and neither opens or closes a citation.
- A backslash before anything else is just a backslash: `C:\tmp` needs no escaping, and no other escape sequence exists.
- A citation may be empty, may contain a newline, and may sit anywhere in the message; there are no other metacharacters.

What that means in C++, where the compiler eats one level of backslashes:

```cpp
error().e(loc, "expected `)`, got `{}`", tok); // two citations; `{}` cites whatever tok renders as
error().e(loc, "write it as \\`foo\\`");       // no citation: two literal backticks
error().e(loc, "C:\\tmp is fine");             // one literal backslash - `\t` is no escape
```

The same markup drives every backend, which is the point of writing it once:

| Backend | `` `x` `` renders as |
| ------- | -------------------- |
| `fe::CodeDiag` on a terminal, `fe::Cli::help`, `fe::Log` | `x`, colored, backticks dropped |
| the same, with color off (`NO_COLOR`, a pipe, `term::Mode::Never`) | `` `x` ``, verbatim |
| `fe::Diag` - the plain-text base | `` `x` ``, verbatim |
| `Cli::markdown` | `` `x` ``, a Markdown code span |

`Cli::markdown` additionally escapes whatever Markdown and Doxygen would otherwise eat (`%`, `|`, `<`, `&`, `--`, ...) - inside a code span and outside it by different rules, both of which are its business, not yours.
You only ever write the two escapes above.

Only the **format string** is markup that way.
**Arguments are data**, and `fe::Error` escapes their backticks for you, so a symbol, path, or token that happens to contain one cannot break the highlighting of the message around it:

```cpp
error().e(loc, "identifier `{}` not found", sym); // sym may be `+ - nothing to do
```

| Where | What to escape |
| ----- | -------------- |
| The format string | Nothing, unless you want a *literal* backtick - spell it `` \` `` - or a literal backslash - `\\`. |
| An argument | Nothing, ever. |
| A message fragment of your own | Nothing - assemble it with `fe::format_cite` and pass the `fe::Cited` it yields; its backticks stay markup while *its* arguments are escaped. |

Markup is a *type*, not a convention you have to remember:

- `fe::cite_string` is a format string whose backticks are markup - that is what `fe::Error::{msg,e,w,n}` take instead of a `std::format_string`; forward one through a wrapper of your own the same way.
- `fe::Cited` is an owning fragment, what `fe::format_cite` yields. Pass it straight back in as an argument - no re-wrapping, and nothing to dangle.
- `fe::Cite` is a *borrowed* fragment, and the type to spell a markup **parameter** with: `Parser`'s `what` and `ctxt` are `Cite`, so a `syntax_err` of your own cannot forward them and silently lose the highlighting.
  A string literal and a `Cited` convert implicitly - both are markup someone wrote - while runtime text has to say `fe::Cite(s)`, so data never becomes markup by accident.
  It borrows like a `std::string_view`, so keep the `Cited` alive that it points at.

The language itself lives in `fe/term.h`, not in the diagnostics: `term::render_cite` renders it, `term::cite_width` measures what it will occupy, `term::escape_cite` escapes data into it, and `term::cite_string`/`term::format_cite`/`term::Cite`/`term::Cited` produce it.
All of them - and `Cli::markdown` - read the grammar above through the one scanner that implements it, so the terminal help, the generated manual, and a diagnostic cannot drift apart.
That is what lets `fe::Cli` spell its `--help` text the same way `fe::Error` spells a diagnostic.
`fe::Log` reads the very same convention, so a log message quotes a plugin, phase, or path exactly the way a diagnostic does.

## 🛠️ Building and Testing

To configure, build, and run the test suite:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

To run one discovered test:

```sh
ctest --test-dir build -R '^Lexer$' --output-on-failure
```

To run a doctest case directly:

```sh
./build/bin/fe-test --test-case=Lexer
```

## 📚 Building the Documentation

To build the documentation:

```sh
cmake -S . -B build -DFE_BUILD_DOCS=ON
cmake --build build --target docs
```

This requires Doxygen and Graphviz (`dot`).

## 🔨 Related Projects

FE is developed against three frontends of very different scale, and every change has to work for all three:

- [Let](https://github.com/leissa/let) - the demo language above, and the template to fork.
- [SQL](https://github.com/leissa/sql) - a SQL parser: two-token lookahead, reserved versus non-reserved words, and anchor-based recovery through comma-separated lists.
- [MimIR](https://anydsl.github.io/MimIR/) - the author's compiler IR: three-token lookahead, a Unicode-heavy surface syntax, and plugins loaded mid-parse that bring their own vocabulary.

In the same spirit:

- [GraphTool](https://github.com/leissa/graphtool) - a DOT-language tool using FE-style frontend infrastructure.

## 🤝 Contributing

Issues and pull requests are welcome - whether that's a bug report, a new frontend building block, or a documentation fix.
If you're unsure where to start, open an issue to discuss the idea first.

## ⚖️ License

FE is licensed under the [MIT License](../LICENSE).
