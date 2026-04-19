# FE repository instructions

## Build, test, and formatting

This repository is a CMake-based **header-only C++20 library**. The main local workflow is:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run a single discovered test with CTest:

```sh
ctest --test-dir build -R '^Lexer$' --output-on-failure
```

Or run a single doctest case directly from the test binary:

```sh
./build/bin/fe-test --test-case=Lexer
```

Documentation is optional and built through the `docs` target:

```sh
cmake -S . -B build -DFE_BUILD_DOCS=ON
cmake --build build --target docs
```

`FE_BUILD_DOCS` requires Doxygen and Graphviz (`dot`).

Formatting/lint-style checks are defined in `.pre-commit-config.yaml`:

```sh
pre-commit run --all-files
```

That runs `clang-format` plus the configured whitespace/YAML hooks. There is no separate CMake lint target.

## High-level architecture

`fe` is exported as a CMake `INTERFACE` target and the public library lives entirely in `include/fe/`. There is no `src/` directory for library implementation; tests build the only executable (`fe-test`).

The library is organized around a few reusable frontend-building blocks that are designed to be composed:

- `fe::Arena` provides arena allocation, an STL allocator adapter, and arena-backed `unique_ptr` support for AST-style ownership.
- `fe::Sym` and `fe::SymPool` intern strings so identifiers can be compared cheaply by pointer after interning.
- `fe::Driver` is the shared frontend context: it inherits `SymPool` and centralizes diagnostics and error/warning counts.
- `fe::Pos` and `fe::Loc` track source positions/locations and are threaded through lexers, parsers, and diagnostics.
- `fe::Ring` is the fixed-size lookahead buffer used by the lexer/parser blueprints.
- `fe::Lexer<K, S>` is a CRTP base that handles UTF-8 decoding, character lookahead, token text accumulation, and source location tracking.
- `fe::Parser<Tok, Tag, K, S>` is a CRTP base that wraps a lexer with token lookahead, `accept`/`expect`/`eat`, and `Tracker` helpers for building node spans.

`tests/lexer.cpp` is the best end-to-end example of intended use: define a token type with `tag()` and `loc()`, derive a concrete lexer/parser from the CRTP bases, use `fe::Driver` for identifier interning and diagnostics, and let locations flow through tokens for error reporting.

## Key conventions

- Keep library code header-only unless you are intentionally changing the project structure. Public headers are listed explicitly in `CMakeLists.txt` and installed from `include/fe/`.
- Default-constructed values are meaningful sentinels across the API: `Tok{}` means parse failure, `Sym{}` is the empty symbol, and default `Pos`/`Loc` are invalid. `Parser::accept` and `Parser::expect` rely on this pattern.
- `Loc::finis` is **inclusive** (the last character in the span), not one-past-the-end. `Loc::path` is a borrowed pointer, so the referenced `std::filesystem::path` must outlive the `Loc`.
- Non-empty symbols should be created through `SymPool::sym` / `Driver::sym`, not by constructing `Sym` manually. Use `SymMap` / `SymSet` aliases instead of concrete hash container types, especially because `FE_ABSL` switches those aliases to Abseil containers.
- Diagnostics are `std::format`-based and go through `fe::Driver::{note,warn,err}`. Follow that pattern rather than inventing separate reporting helpers.
- If a type already has `operator<<`, expose it to `std::format` with `template<> struct std::formatter<T> : fe::ostream_formatter {};`.
- Derived lexers typically pull CRTP base helpers into scope with `using` declarations (`ahead`, `accept`, `next`, `loc_`, `peek_`, `str_`), matching the pattern in `tests/lexer.cpp`.
- `fe/loc.h` only declares `operator<<` for `Pos` and `Loc`; include `fe/loc.cpp.h` in exactly one translation unit when you want the default streaming implementation.
