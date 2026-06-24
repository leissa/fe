# CLAUDE.md

`fe` is a CMake-based, **header-only C++ library** of reusable building blocks for writing language
frontends (arena allocation, string interning, source locations, UTF-8 lexer/parser CRTP bases,
diagnostics). It is consumed as a git submodule (this checkout lives under `submodules/fe`).

> Most of this file is reused from `.github/copilot-instructions.md`. Keep the two in sync when you
> change build/architecture facts.

## Build, test, and formatting

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

Tests need the bundled submodules. If `build` configure fails on a missing `submodules/doctest`,
run `git submodule update --init --recursive` first.

Documentation is optional and built through the `docs` target (requires Doxygen + Graphviz `dot`):

```sh
cmake -S . -B build -DFE_BUILD_DOCS=ON
cmake --build build --target docs
```

Formatting/lint checks live in `.pre-commit-config.yaml` (`clang-format` + whitespace/YAML hooks).
There is no separate CMake lint target:

```sh
pre-commit run --all-files
```

## Build options & toolchain

- The library requires **C++23** (`target_compile_features(fe INTERFACE cxx_std_23)` in
  `CMakeLists.txt`). The prose elsewhere may say C++20 — trust the CMake setting.
- `FE_ABSL` (default `OFF`): switches `SymMap`/`SymSet` and friends from `std` to Abseil containers.
- `FE_BUILD_DOCS` (default `OFF`): build Doxygen docs.
- `BUILD_TESTING` (CTest default `ON`): builds the only executable, `fe-test`.
- MSVC: `CMakeLists.txt` adds `/utf-8 /wd4146 /wd4245` and `_CTYPE_DISABLE_MACROS`. Keep new headers
  MSVC-clean; UTF-8 source handling is assumed.

## High-level architecture

`fe` is exported as a CMake `INTERFACE` target; the public library lives entirely in `include/fe/`.
There is no `src/` — tests build the only executable. Headers are listed explicitly in
`CMakeLists.txt` and installed from `include/fe/`.

Composable frontend building blocks:

- `fe::Arena` (`arena.h`) — arena allocation, STL allocator adapter, arena-backed `unique_ptr` for AST ownership.
- `fe::Sym` / `fe::SymPool` (`sym.h`) — intern strings so identifiers compare cheaply by pointer.
- `fe::Driver` (`driver.h`) — shared frontend context; inherits `SymPool`, centralizes diagnostics + error/warning counts.
- `fe::Pos` / `fe::Loc` (`loc.h`, `loc.cpp.h`) — source positions/locations threaded through lexers, parsers, diagnostics.
- `fe::Ring` (`ring.h`) — fixed-size lookahead buffer used by the lexer/parser blueprints.
- `fe::Lexer<K, S>` (`lexer.h`) — CRTP base: UTF-8 decoding, char lookahead, token-text accumulation, location tracking.
- `fe::Parser<Tok, Tag, K, S>` (`parser.h`) — CRTP base wrapping a lexer with token lookahead, `accept`/`expect`/`eat`, `Tracker` span helpers.

Support headers: `assert.h` (`assert`/`assertf`/`unreachable`), `cast.h` (checked/dynamic casts),
`enum.h` (bit-flag enum ops), `format.h` (`ostream_formatter`, `std::format` glue), `term.h`
(terminal/ANSI color), `utf8.h` (UTF-8 decode primitives).

`tests/lexer.cpp` is the best end-to-end example: define a token type with `tag()`/`loc()`, derive a
concrete lexer/parser from the CRTP bases, use `fe::Driver` for interning + diagnostics, let
locations flow through tokens for error reporting.

## Key conventions

- Keep library code header-only unless intentionally changing project structure.
- Default-constructed values are meaningful sentinels: `Tok{}` = parse failure, `Sym{}` = empty
  symbol, default `Pos`/`Loc` are invalid. `Parser::accept`/`expect` rely on this.
- `Loc::finis` is **inclusive** (last char in the span), not one-past-the-end. `Loc::path` is a
  borrowed pointer — the referenced `std::filesystem::path` must outlive the `Loc`.
- Create non-empty symbols via `SymPool::sym` / `Driver::sym`, never by constructing `Sym` manually.
  Use the `SymMap` / `SymSet` aliases (not concrete container types), since `FE_ABSL` swaps them.
- Diagnostics are `std::format`-based and go through `fe::Driver::{note,warn,err}`. Follow that
  pattern; don't invent separate reporting helpers.
- If a type has `operator<<`, expose it to `std::format` with
  `template<> struct std::formatter<T> : fe::ostream_formatter {};`.
- Derived lexers pull CRTP base helpers into scope with `using` declarations (`ahead`, `accept`,
  `next`, `loc_`, `peek_`, `str_`), as in `tests/lexer.cpp`.
- `fe/loc.h` only declares `operator<<` for `Pos`/`Loc`; include `fe/loc.cpp.h` in exactly one
  translation unit for the default streaming implementation.
