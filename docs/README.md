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
Most of it is header-only; the handful of components that need a translation unit of their own come with `FE_LIB`, which is on by default.

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

### Diagnostics

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

### And you can read it afterwards

There is no code generation step, so there is no generated code to debug and no build-time dependency on a tool.
`parse_expr` is a function that says what it does, in the language the rest of your compiler is written in.

## ✨ Features

Handwritten frontends are often the right choice when you want full control over syntax, diagnostics, recovery, and architecture. FE embraces that style.

It provides a compact set of reusable, well-integrated components:

### Building Blocks

Header-only, except for what [Requires `FE_LIB`](#requires-fe_lib) lists below.

#### Core

- `fe::Driver` for shared frontend state: the SymPool, the SrcMap, the interned `Dbg`s, the `Diag` that lays a diagnostic out, and the `Error` everything reports into.
  Global variables in all but name - which is the point: they live in one object you own and pass around, not in the global namespace.
- `fe::Arena` for fast arena allocation and arena-backed ownership.
- `fe::Sym` and `fe::SymPool` for string interning and cheap identifier comparison.

#### Lexing & Parsing

- `fe::Lexer<K, S>` for UTF-8-aware lexing with lookahead and token text accumulation.
- `fe::Parser<Tok, Tag, K, S>` for recursive-descent-style parsing with token lookahead, span tracking, and anchor-based error recovery.
  Both blueprints ask their child for a `fe::Driver& driver()` and report their default diagnostics into its `Error`; a header-only setup words all of them itself and never touches a `Driver`.
- `fe::utf8` for lightweight UTF-8 handling.

#### Diagnostics

- `fe::Pos` and `fe::Loc` for source positions and source spans.
- `fe::Src` and `fe::SrcMap` for owning source text and resolving a position back to `path:row:col`.
- `fe::Dbg` for the `Loc`/`Sym` pair every named entity drags along, interned in the `Driver` as a `DbgKey`.
- `fe::Error` for collecting diagnostics - errors, warnings and their notes - and rendering each with its source snippet; `Error::ack` throws what it collected as a self-contained `Error::Bail`.
  The `Driver` owns the one everything reports into (`Driver::error`); `Error::{e,w,n}` are terse aliases of `Error::{error,warn,note}`, in the spirit of `Log::e` and friends.
- `fe::Diag` for how a diagnostic lays out: `Diag::loc_style` (a `Loc::Style`), `Diag::no_snippet`, and friends cover the usual adjustments, and one virtual per piece (`loc`, `header`, `snippet`, `note`, `summary`, `render`) covers the rest.
  Derive and `Driver::diag(std::make_unique<MyDiag>())` to lay one out entirely your own way.
- `fe::Log` for leveled logging with acronym, color, and origin prefix.
    - `Log::error`/`Log::warn`/... shorthands point at their call site via `std::source_location`; no macros involved.
- `fe::term` for lightweight terminal colors in diagnostics and CLI output.

#### Command Line

- `fe::Cli` for parsing `argc`/`argv` of a single command: chain `Cli::opt`/`Cli::arg` and bind each switch to a variable of yours - a `bool`, a `std::string`, an integral, a `std::vector` of those, or a callable.
  It understands `--name value`, `--name=value`, `-n value`, `-nvalue`, clustered short flags, and `--`.
- `Cli::help` lays those switches out for a terminal - grouped into sections by `Cli::grp`, wrapped to the terminal width, and colored via `fe::term`.
  `Cli::md` renders the very same information as Markdown tables, so `--help` and the manual cannot drift apart.
- `Cli::section` for titled `term`/description rows that aren't options - `ENVIRONMENT`, plugin arguments, and the like - rendered below the options in both backends.

#### Data Structures

- `fe::Span`/`fe::View` and `fe::Vector` for spans with structured binding and small-buffer vectors.
- `fe::Bitset` for a dynamically growing bit set that keeps small sets inline and only allocates once they grow.
- `fe::XTrie` for interned, immutable sets - an [IndexedTrie](https://dl.acm.org/doi/10.1145/3808286) that is space-efficient and answers intersection tests fast.
- `fe::BFSWorklist`/`fe::DFSWorklist` for worklist traversals that visit each element at most once.
- Optional `FE_ABSL` support for [Abseil](https://abseil.io/) hash containers.

#### Odds & Ends

- `fe::hash` and friends for cheap, `constexpr` hash mixing/combining.
- `fe::Restore` for RAII save/restore of a variable - or of anything a getter/setter pair reaches, like `term::ScopedMode` - across a scope.
- `fe/algo.h` and `fe/container.h` for the odds and ends every frontend rewrites otherwise.

### Requires `FE_LIB` {#requires-fe_lib}

These need a translation unit of their own and hence live in `src/fe/`:

- `fe::Diag` for the diagnostic layout - and hence `fe::Driver`, whose ctor/dtor own one, and `Error::diag`, which reads it back out of the `Driver`.
  The default `syntax_err`/`unanchored_err`/`utf8_err`/`char_err` of `fe::Parser`/`fe::Lexer` go through this, so a header-only frontend has to supply its own.
- `fe::Snippet` for the underlined source excerpt below a diagnostic.
- `fe::dl` and `fe::sys` for loading dynamic libraries and locating/running external commands.
- `fe::Profiler` for nested wall-clock spans reported as a flat table, a tree, or Chrome Trace JSON.
- The default `operator<<`/`dump` of `fe::Pos`/`fe::Loc`.

  `fe/loc.h` merely *declares* these.
  So in a header-only setup you have to hand-roll your own rendering - as a hidden friend, it must be defined in namespace `fe`:

  ```cpp
  namespace fe {

  std::ostream& operator<<(std::ostream& os, Loc loc) { /* ... */ }
  std::ostream& operator<<(std::ostream& os, Pos pos) { /* ... */ }

  void Loc::dump() const { std::cout << *this << std::endl; }
  void Pos::dump() const { std::cout << *this << std::endl; }

  } // namespace fe
  ```

  Otherwise you will run into a link error for `operator<<(std::ostream&, fe::Loc)` and friends.

FE does not try to hide frontend construction behind a generator.
Instead, it gives you sharp, reusable tools so you can build exactly the frontend you want.

For a complete end-to-end example, see [**Let**](https://github.com/leissa/let), a small toy language built on FE.

## 🚀 Quick Start

The easiest way to get going is through [**Let**](https://github.com/leissa/let).

You can either:

- 📦 create a [new repository from the Let template](https://github.com/new?template_owner=leissa&template_name=let), or
- 🍴 [fork Let directly](https://github.com/leissa/let/fork).

That gives you a concrete, working example of how FE is intended to be used in practice.

### Integrate into existing Project

#### CMake

Add FE as a subdirectory and link the `fe` target:

```cmake
add_subdirectory(submodules/fe)
target_link_libraries(my_compiler PRIVATE fe)
```

Set any of the options below *before* adding the subdirectory:

```cmake
set(FE_LIB  OFF) # header-only building blocks only - no fe::Driver, fe::Error, or fe::Diag
set(FE_ABSL ON) # use Abseil-backed hash containers
add_subdirectory(submodules/fe)
target_link_libraries(my_compiler PRIVATE fe)
```

`FE_LIB` compiles `src/fe/` along with the headers and is on by default.
Turn it off to get only the header-only building blocks; you lose the components listed under [Requires `FE_LIB`](#requires-fe_lib).

`fe-lib` is an `OBJECT` library, so its symbols land inside a shared library of *yours*.
On Windows that shared library has to export them, which CMake cannot infer: compile everything that goes into it with `fe_lib_EXPORTS`, or `FE_STATIC_DEFINE` if there is no shared library in play.

```cmake
target_compile_definitions(my_compiler PRIVATE fe_lib_EXPORTS)
```

#### Direct Vendoring

You can also vendor `include/fe/` directly into your project and add the `src/fe/*.cpp` you need to your build - `fe::dl` additionally wants `${CMAKE_DL_LIBS}`.

If you want Abseil support in that setup, compile with:

```sh
-DFE_ABSL
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

## 🛠️ Building and Testing

To configure, build, and run the test suite:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests need `FE_LIB`, which is on by default.

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

- [Let](https://github.com/leissa/let) - the 619-line demo language above, and the template to fork.
- [SQL](https://github.com/leissa/sql) - a SQL parser: two-token lookahead, reserved versus non-reserved words, and anchor-based recovery through comma-separated lists.
- [MimIR](https://anydsl.github.io/MimIR/) - the author's compiler IR: three-token lookahead, a Unicode-heavy surface syntax, and plugins loaded mid-parse that bring their own vocabulary.

In the same spirit:

- [GraphTool](https://github.com/leissa/graphtool) - a DOT-language tool using FE-style frontend infrastructure.

## 🤝 Contributing

Issues and pull requests are welcome - whether that's a bug report, a new frontend building block, or a documentation fix.
If you're unsure where to start, open an issue to discuss the idea first.

## ⚖️ License

FE is licensed under the [MIT License](../LICENSE).
