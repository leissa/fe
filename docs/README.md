# FE

[![Linux](https://img.shields.io/github/actions/workflow/status/leissa/fe/linux.yml?style=flat-square&logo=linux&label=linux&logoColor=white&branch=main)](https://github.com/leissa/fe/actions/workflows/linux.yml)
[![Windows](https://img.shields.io/github/actions/workflow/status/leissa/fe/windows.yml?style=flat-square&label=⊞%20windows&branch=main)](https://github.com/leissa/fe/actions/workflows/windows.yml)
[![macOS](https://img.shields.io/github/actions/workflow/status/leissa/fe/macos.yml?style=flat-square&logo=apple&label=macos&branch=main)](https://github.com/leissa/fe/actions/workflows/macos.yml)
[![Doxygen](https://img.shields.io/github/actions/workflow/status/leissa/fe/doxygen.yml?style=flat-square&logo=gitbook&logoColor=white&label=doxygen&branch=main)](https://github.com/leissa/fe/actions/workflows/doxygen.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square&logo=opensourceinitiative&logoColor=white)](../LICENSE)

[TOC]

**FE** is a header-only C++20 toolkit for writing hand-rolled compiler and interpreter frontends.
It does not generate lexers or parsers for you; instead, it gives you the reusable pieces that make handwritten frontends pleasant to build and maintain.

## 💡 Why FE?

FE is built around a few small components that compose well:

- `fe::Arena` for arena allocation and arena-backed ownership.
- `fe::Sym` and `fe::SymPool` for string interning and cheap identifier comparison.
- `fe::Driver` for diagnostics and shared frontend state.
- `fe::Pos` and `fe::Loc` for source positions and spans.
- `fe::Lexer<K, S>` for UTF-8-aware lexing with lookahead and token text accumulation.
- `fe::Parser<Tok, Tag, K, S>` for recursive-descent style parsing with token lookahead and span tracking.
- Optional `FE_ABSL` support for Abseil hash containers.

The best end-to-end example in this repository is [`tests/lexer.cpp`](../tests/lexer.cpp).

## 🚀 Quick start

### CMake

Add FE as a subdirectory and link the `fe` interface target:

```cmake
add_subdirectory(external/fe)
target_link_libraries(my_compiler PRIVATE fe)
```

If you want Abseil-backed hash containers, enable `FE_ABSL` before adding the subdirectory:

```cmake
set(FE_ABSL ON)
add_subdirectory(external/fe)
target_link_libraries(my_compiler PRIVATE fe)
```

### Direct include

Since FE is header-only, you can also vendor `include/fe/` directly into your project and add `-DFE_ABSL` if you want Abseil support.

## 🧭 Typical workflow

1. Define a token type that exposes `tag()` and `loc()`.
2. Derive your lexer from `fe::Lexer<K, S>`.
3. Derive your parser from `fe::Parser<Tok, Tag, K, S>`.
4. Use `fe::Driver` for diagnostics and identifier interning.
5. Thread `fe::Loc` through tokens and AST nodes for precise error reporting.

If you want a concrete model to copy from, start with [`tests/lexer.cpp`](../tests/lexer.cpp).

## 🛠️ Building and testing FE itself

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run one discovered test:

```sh
ctest --test-dir build -R '^Lexer$' --output-on-failure
```

Or run a doctest case directly:

```sh
./build/bin/fe-test --test-case=Lexer
```

## 📚 Building the documentation

```sh
cmake -S . -B build -DFE_BUILD_DOCS=ON
cmake --build build --target docs
```

This requires Doxygen and Graphviz (`dot`).

## 🔨 Related projects

- [Let](https://github.com/leissa/let) - a small demo language built on FE.
- [GraphTool](https://github.com/leissa/graphtool) - a DOT-language tool using FE-style frontend infrastructure.
- [MimIR](https://anydsl.github.io/MimIR/) - an intermediate representation project by the author.
- [SQL](https://github.com/leissa/sql) - a small SQL parser.

## ⚖️ License

FE is licensed under the [MIT License](../LICENSE).
