# FE repository instructions

`fe` is a CMake-based, **header-only C++ library** of reusable building blocks for writing language frontends (arena allocation, string interning, source locations, UTF-8 lexer/parser CRTP bases, diagnostics). It is typically consumed as a git submodule (a checkout may live under e.g. `submodules/fe`).

## Build, test, and formatting

The main local workflow is:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests need the bundled submodules. If configure fails on a missing `submodules/doctest`, run `git submodule update --init --recursive` first.

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

## Build options & toolchain

- The library requires **C++23** (`target_compile_features(fe INTERFACE cxx_std_23)` in `CMakeLists.txt`). The prose elsewhere may say C++20 — trust the CMake setting.
- `FE_ABSL` (default `OFF`): switches `SymMap`/`SymSet` and friends from `std` to Abseil containers.
- `FE_BUILD_DOCS` (default `OFF`): build Doxygen docs (requires Doxygen + Graphviz `dot`).
- `BUILD_TESTING` (CTest default `ON`): builds the only executable, `fe-test`.
- MSVC: `CMakeLists.txt` adds `/utf-8 /wd4146 /wd4245` and `_CTYPE_DISABLE_MACROS`. Keep new headers MSVC-clean; UTF-8 source handling is assumed.

## High-level architecture

`fe` is exported as a CMake `INTERFACE` target and the public library lives entirely in `include/fe/`. There is no `src/` directory for library implementation; tests build the only executable (`fe-test`).

The library is organized around a few reusable frontend-building blocks that are designed to be composed:

- `fe::Arena` (`arena.h`) provides arena allocation, an STL allocator adapter, and arena-backed `unique_ptr` support for AST-style ownership.
- `fe::Sym` and `fe::SymPool` (`sym.h`) intern strings so identifiers can be compared cheaply by pointer after interning.
- `fe::Driver` (`driver.h`) is the shared frontend context: it inherits `SymPool` and centralizes diagnostics and error/warning counts.
- `fe::Pos` and `fe::Loc` (`loc.h`, `loc.cpp.h`) track source positions/locations and are threaded through lexers, parsers, and diagnostics.
- `fe::Src` and `fe::SrcMap` (`src.h`) own the text of each source file and turn a `Pos` back into a row/column. A `Loc` borrows a `const Src*`, which is how it renders itself as `path:row:col`.
- `fe::Ring` (`ring.h`) is the fixed-size lookahead buffer used by the lexer/parser blueprints.
- `fe::Lexer<K, S>` (`lexer.h`) is a CRTP base that handles UTF-8 decoding, character lookahead, token text accumulation, and source location tracking.
- `fe::Parser<Tok, Tag, K, S>` (`parser.h`) is a CRTP base that wraps a lexer with token lookahead, `accept`/`expect`/`eat`, and `Tracker` helpers for building node spans.

Support headers: `assert.h` (`assert`/`assertf`/`unreachable`), `cast.h` (checked/dynamic casts), `enum.h` (bit-flag enum ops), `format.h` (`ostream_formatter`, `std::format` glue), `hash.h` (`constexpr` hash mixing/combining), `term.h` (terminal/ANSI color), `utf8.h` (UTF-8 decode primitives).

`tests/lexer.cpp` is the best end-to-end example of intended use: define a token type with `tag()` and `loc()`, derive a concrete lexer/parser from the CRTP bases, use `fe::Driver` for identifier interning and diagnostics, and let locations flow through tokens for error reporting.

## Key conventions

- Keep library code header-only unless you are intentionally changing the project structure. Public headers are listed explicitly in `CMakeLists.txt` and installed from `include/fe/`.
- Default-constructed values are meaningful sentinels across the API: `Tok{}` means parse failure, `Sym{}` is the empty symbol, and default `Pos`/`Loc` are invalid. `Parser::accept` and `Parser::expect` rely on this pattern.
- `Loc::end` is **exclusive** (the byte one past the span), just like an STL iterator. `Loc::src` is a borrowed `const Src*`, so the `Src` must outlive the `Loc`; a `SrcMap` owns one for you.
- `SrcMap` interns paths under `SrcMap::key` (absolute, symlink-free, normalized), so one file yields exactly one `Src`. That is what lets `Loc` compare files by pointer - do not hand a `Loc` a `Src` that some other `SrcMap` (or nobody) owns.
- A `Loc` renders itself: `operator<<`/`std::format` spell out `path:row:col-row:col` via `Loc::src`, falling back to `path@begin-end` when it has no `Src` or the offsets do not fit. Diagnostics just pass the `Loc`.
- Non-empty symbols should be created through `SymPool::sym` / `Driver::sym`, not by constructing `Sym` manually. Use `SymMap` / `SymSet` aliases instead of concrete hash container types, especially because `FE_ABSL` switches those aliases to Abseil containers.
- Diagnostics are `std::format`-based and go through `fe::Driver::{note,warn,err}`. Follow that pattern rather than inventing separate reporting helpers.
- If a type already has `operator<<`, expose it to `std::format` with `template<> struct std::formatter<T> : fe::ostream_formatter {};`.
- Derived lexers typically pull CRTP base helpers into scope with `using` declarations (`ahead`, `accept`, `next`, `loc_`, `peek`, `str_`), matching the pattern in `tests/lexer.cpp`.
- `fe/loc.h` only declares `operator<<` for `Pos` and `Loc`; include `fe/loc.cpp.h` in exactly one translation unit when you want the default streaming implementation. That header pulls in `fe/src.h`, since `Src` is only forward-declared in `fe/loc.h`.

## Comments

Doxygen comments (`///`, `/** ... */`) on the public API in `include/fe/` are documentation, not commentary: they are expected and exempt from the rules below.
Prefer one sentence per line over column-filling wraps there, so diffs stay readable.
Everything else - comments inside function bodies, in tests, in CMake - follows these rules.

Comments are scarce. The default is **no comment**.

Comment only when the code itself cannot reasonably express the information.

- Comment **why**, not what the code does.
- Prefer a better name, structure, or API over a comment.
- Keep comments to **one short sentence**, normally one line.
- A comment should convey one fact only: an invariant, non-obvious constraint, algorithmic reason, or important external reference.
- Do not explain the implementation, summarize a function, or provide a narrative of its control flow.
- Match the comment density and brevity of the surrounding code. **Never increase comment density.**
- Do not add documentation-style prose, introductions, conclusions, or motivational/explanatory language.
- Do not use rhetorical contrasts such as `"X" -> "Y"`, `"instead of X"`, or `"from X to Y"` to explain an optimization.
- Do not add comments describing the change itself ("now handles X", "renamed from Y"); that belongs in the commit message.
- Do not add banner or section-header comments.
- Do not add a comment if deleting it would leave the code equally correct and understandable.
- A comment that restates the code is worse than no comment:
  ```cpp
  vec.push_back(x); // BAD: "put x into the vector"
  ```

**Hard limit:** Do not write multi-line comments unless the user explicitly asks for documentation or the comment is required to document a non-obvious invariant that cannot be stated briefly.

Before adding a comment, ask:
1. Is this information necessary?
2. Is it already apparent from the code or names?
3. Can it be expressed in one short sentence?
If the answer to 1 or 3 is no, do not add the comment.
