# FE

[![GitHub Release](https://img.shields.io/github/v/release/leissa/fe?style=flat-square&logo=starship)](https://github.com/leissa/fe/releases)
[![Linux](https://img.shields.io/github/actions/workflow/status/leissa/fe/linux.yml?style=flat-square&logo=linux&label=linux&logoColor=white&branch=main)](https://github.com/leissa/fe/actions/workflows/linux.yml)
[![Windows](https://img.shields.io/github/actions/workflow/status/leissa/fe/windows.yml?style=flat-square&label=⊞%20windows&branch=main)](https://github.com/leissa/fe/actions/workflows/windows.yml)
[![macOS](https://img.shields.io/github/actions/workflow/status/leissa/fe/macos.yml?style=flat-square&logo=apple&label=macos&branch=main)](https://github.com/leissa/fe/actions/workflows/macos.yml)
[![Doxygen](https://img.shields.io/github/actions/workflow/status/leissa/fe/doxygen.yml?style=flat-square&logo=gitbook&logoColor=white&label=doxygen&branch=main)](https://github.com/leissa/fe/actions/workflows/doxygen.yml)
[![Documentation](https://img.shields.io/badge/docs-master-green?style=flat-square&logo=gitbook&logoColor=white)](https://leissa.github.io/fe)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?style=flat-square&logo=cplusplus)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square&logo=opensourceinitiative&logoColor=white&color=yellowgreen)](../LICENSE)

[TOC]

**FE** is a header-only C++23 toolkit for building handwritten compiler and interpreter frontends.

Rather than generating lexers or parsers for you, FE focuses on the infrastructure that every frontend needs anyway: source locations, diagnostics, interning, parsing support, and efficient memory management.
The goal is simple: keep handwritten frontends lightweight, explicit, and pleasant to maintain.

## 💡 Why FE?

FE is a good fit if you want to build:

- a small programming language or DSL,
- a hand-written recursive-descent parser,
- a lexer with precise UTF-8-aware source tracking,
- a frontend with high-quality diagnostics,
- a prototype compiler or interpreter that should stay easy to evolve.

It is especially useful when you want the flexibility of handwritten code without repeatedly rebuilding the same frontend infrastructure from scratch.

## ✨ Features

Handwritten frontends are often the right choice when you want full control over syntax, diagnostics, recovery, and architecture. FE embraces that style.

It provides a compact set of reusable, well-integrated components:

- `fe::Arena` for fast arena allocation and arena-backed ownership.
- `fe::Sym` and `fe::SymPool` for string interning and cheap identifier comparison.
- `fe::Driver` for diagnostics and shared frontend state.
- `fe::Pos` and `fe::Loc` for source positions and source spans.
- `fe::term` for lightweight terminal colors in diagnostics and CLI output.
- `fe::utf8` for lightweight UTF-8 handling.
- `fe::Lexer<K, S>` for UTF-8-aware lexing with lookahead and token text accumulation.
- `fe::Parser<Tok, Tag, K, S>` for recursive-descent-style parsing with token lookahead and span tracking.
- Optional `FE_ABSL` support for [Abseil](https://abseil.io/) hash containers.

FE does not try to hide frontend construction behind a generator.
Instead, it gives you sharp, reusable tools so you can build exactly the frontend you want.

For a complete end-to-end example, see [**Let**](https://github.com/leissa/let), a small toy language built on FE..

## 🚀 Quick Start

The easiest way to get going is through [**Let**](https://github.com/leissa/let).

You can either:

- 📦 create a [new repository from the Let template](https://github.com/new?template_owner=leissa&template_name=let), or
- 🍴 [fork Let directly](https://github.com/leissa/let/fork).

That gives you a concrete, working example of how FE is intended to be used in practice.

### Integrate into existing Project

#### CMake

Add FE as a subdirectory and link the `fe` interface target:

```cmake
add_subdirectory(submodules/fe)
target_link_libraries(my_compiler PRIVATE fe)
```

If you want Abseil-backed hash containers, enable `FE_ABSL` before adding the subdirectory:

```cmake
set(FE_ABSL ON)
add_subdirectory(submodules/fe)
target_link_libraries(my_compiler PRIVATE fe)
```

#### Direct Vendoring

Because FE is header-only, you can also vendor `include/fe/` directly into your project.

If you want Abseil support in that setup, compile with:

```sh
-DFE_ABSL
```

## 🧭 Typical Workflow

A typical FE-based frontend looks roughly like this:

1. Define a token type exposing `tag()` and `loc()`.
2. Implement your lexer by deriving from `fe::Lexer<K, S>`.
3. Implement your parser by deriving from `fe::Parser<Tok, Tag, K, S>`.
4. Use `fe::Driver` to centralize diagnostics and shared state.
5. Thread `fe::Loc` through tokens and AST nodes for precise error reporting.
6. Use `fe::Arena` and symbol interning where allocation cost and identifier handling matter.

If you want a concrete model to copy from, start with [`tests/lexer.cpp`](../tests/lexer.cpp).

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

A few projects that use or reflect the same frontend philosophy:

- [Let](https://github.com/leissa/let) - a small demo language built on FE.
- [MimIR](https://anydsl.github.io/MimIR/) - an intermediate representation project by the author.
- [GraphTool](https://github.com/leissa/graphtool) - a DOT-language tool using FE-style frontend infrastructure.
- [SQL](https://github.com/leissa/sql) - a small SQL parser.

## ⚖️ License

FE is licensed under the [MIT License](../LICENSE).
