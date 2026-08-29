# FE repository instructions

`fe` is a CMake-based C++ library of reusable building blocks for writing language frontends (arena allocation, string interning, source locations, UTF-8 lexer/parser CRTP bases, diagnostics).
It is **header-only by default**; `FE_LIB=ON` additionally builds `fe-lib` with the handful of components that cannot be (see below).
It is typically consumed as a git submodule (a checkout may live under e.g. `submodules/fe`).

## Build, test, and formatting

The main local workflow is:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests need the bundled submodules. If configure fails on a missing `submodules/doctest`, run `git submodule update --init --recursive` first.

Run a single test either through CTest (`ctest --test-dir build -R '^Lexer$' --output-on-failure`) or directly from the test binary (`./build/bin/fe-test --test-case=Lexer`).

Documentation is optional and built through the `docs` target; `FE_BUILD_DOCS` requires Doxygen and Graphviz (`dot`):

```sh
cmake -S . -B build -DFE_BUILD_DOCS=ON
cmake --build build --target docs
```

Formatting/lint-style checks are defined in `.pre-commit-config.yaml` and run via `pre-commit run --all-files`: `clang-format` (see `.clang-format`) plus the configured whitespace/YAML hooks. There is no separate CMake lint target.

CI (`.github/workflows/`) builds Linux (gcc-14 and clang, Debug and Release), macOS, and Windows, and runs `fe-test` under Valgrind as well as ASan/LSan/UBSan.
A change is only done when it is leak- and UB-clean, not merely when `ctest` passes.

## Build options & toolchain

- The library requires **C++23** (`target_compile_features` in `CMakeLists.txt`).
- `FE_LIB` (default `OFF`): additionally builds `fe-lib`, an `OBJECT` library over `src/fe/`. A standalone `BUILD_TESTING` build forces it ON, because the tests rely on the default `Loc` rendering.
- `FE_ABSL` (default `OFF`): switches `SymMap`/`SymSet`/`PathMap` and friends from `std` to Abseil containers.
- `FE_BUILD_DOCS` (default `OFF`): build Doxygen docs (requires Doxygen + Graphviz `dot`).
- `BUILD_TESTING` (CTest default `ON`): builds the only executable, `fe-test`.
- MSVC: `CMakeLists.txt` adds `/utf-8 /wd4146 /wd4245` and `_CTYPE_DISABLE_MACROS`. Keep new headers MSVC-clean; UTF-8 source handling is assumed.

## High-level architecture

The public API lives entirely in `include/fe/`. `fe` is always an `INTERFACE` target carrying the header usage requirements; `FE_LIB=ON` adds `fe-lib`, an `OBJECT` library over `src/fe/` that links `fe` publicly. Tests build the only executable (`fe-test`).

The library is organized around a few reusable frontend-building blocks that are designed to be composed:

- `fe::Arena` (`arena.h`) provides arena allocation, an STL allocator adapter, and arena-backed `unique_ptr` support for AST-style ownership.
- `fe::Sym` and `fe::SymPool` (`sym.h`) intern strings so identifiers can be compared cheaply by pointer after interning.
- `fe::Driver` (`driver.h`) is the shared frontend context: it inherits `SymPool`, owns the `SrcMap` (`Driver::src`) and the interned `Dbg`s (`Driver::dbg`), and inherits `Diagnostics` (`diag.h`) for the diagnostic layout (`Driver::diag`) plus the `render` hook an `Error` formats each message through.
  An `Error` only needs the `Diagnostics`, not a whole `Driver`.
- `fe::Error` (`error.h`) collects `std::format`-based diagnostics - errors and warnings, each owning the notes that hang off it - and renders each with the `Snippet` its `Loc` points at.
  It is a sink, not an exception: `Error::ack`/`Error::bail` render everything and throw the text as an `Error::Bail`, which no longer points into any `Src` and may propagate past the `Driver`.
- `fe::Pos` and `fe::Loc` (`loc.h`) track source positions/locations and are threaded through lexers, parsers, and diagnostics.
- `fe::Src` and `fe::SrcMap` (`src.h`) own the text of each source file and turn a `Pos` back into a row/column. A `Loc` borrows a `const Src*`, which is how it renders itself as `path:row:col`.
- `fe::Ring` (`ring.h`) is the fixed-size lookahead buffer used by the lexer/parser blueprints.
- `fe::XTrie` (`xtrie.h`) hash-conses sets of pointers - small ones as sorted arrays, large ones as paths in an [IndexedTrie](https://dl.acm.org/doi/10.1145/3808286) built on the intrusive link-cut-tree of `lct.h` - so that equal sets are pointer-equal.
  A *key* trait passed as template argument tells it how to read the `gid`/`tid` of an element.
- `fe::Lexer<K, S>` (`lexer.h`) is a CRTP base that handles UTF-8 decoding, character lookahead, token text accumulation (`str_`), and source location tracking (`loc_`).
- `fe::Parser<Tok, Tag, K, S>` (`parser.h`) is a CRTP base that wraps a lexer with token lookahead, `accept`/`expect`/`eat`, `Tracker` helpers for building node spans, and the anchor-based error recovery described below.

Support headers: `algo.h` (bit casts, padding, small string/range algorithms), `assert.h` (`assert`/`assertf`/`unreachable`), `cast.h` (checked/dynamic casts), `container.h` (`pop`/`lookup` helpers, `fe::UniqueQueue`), `dbg.h` (`fe::Dbg`, a `Loc`/`Sym` pair), `diag.h` (`fe::Diag` layout and the `fe::Diagnostics` render hook), `enum.h` (bit-flag enum ops), `format.h` (`ostream_formatter`, `std::format` glue), `hash.h` (`constexpr` hash mixing/combining), `log.h` (`fe::Log`, leveled logging) and `log_macros.h` (its `ELOG`/`WLOG`/... shorthands, separated so `log.h` stays macro-free), `restore.h` (`fe::Restore`, an RAII guard that restores a reference at end of scope), `span.h` (`fe::Span`/`fe::View`), `term.h` (terminal/ANSI color, incl. `fe::term::ScopedMode`), `utf8.h` (UTF-8 decode primitives), `vector.h` (`fe::Vector`, small-buffer vector).

`fe-lib` holds the components that need a translation unit of their own: the default `Pos`/`Loc` streaming and `dump` (`loc.h`), `fe::Snippet` (`snippet.h`), `fe::dl` (`dl.h`, dynamic library loading), `fe::sys` (`sys.h`, locating and running external commands), and `fe::Profiler` (`profile.h`, nested wall-clock spans reported as a flat table, a tree, or Chrome Trace JSON).
It is an `OBJECT` library on purpose: link it into exactly one shared library of yours and every other consumer resolves those symbols there instead of carrying a copy.

`tests/lexer.cpp` is the best end-to-end example of intended use: define a token type with `tag()` and `loc()`, derive a concrete lexer/parser from the CRTP bases, use `fe::Driver` for identifier interning and an `fe::Error` for diagnostics, and let locations flow through tokens for error reporting.

## Key conventions

- Keep library code header-only unless it genuinely cannot be; then declare it in `include/fe/` and implement it in `src/fe/`, which is what `fe-lib` compiles. Public headers are listed explicitly in `CMakeLists.txt` and installed from `include/fe/`.
- Default-constructed values are meaningful sentinels across the API: `Tok{}` means parse failure, `Sym{}` is the empty symbol, and default `Pos`/`Loc` are invalid. `Parser::accept` and `Parser::expect` rely on this pattern.
- `Loc::end` is **exclusive** (the byte one past the span), just like an STL iterator. `Loc::src` is a borrowed `const Src*`, so the `Src` must outlive the `Loc`; a `SrcMap` owns one for you.
- `Loc` is kept at two machine words (`static_assert` in `loc.h`) so it stays a value passed in registers - do not grow it.
- `SrcMap` interns paths under `SrcMap::key` (absolute, symlink-free, normalized), so one file yields exactly one `Src`. That is what lets `Loc` compare files by pointer - do not hand a `Loc` a `Src` that some other `SrcMap` (or nobody) owns.
- A `Loc` renders itself: `operator<<`/`std::format` spell out `path:row:col-row:col` via `Loc::src`, falling back to `path@begin-end` when it has no `Src` or the offsets do not resolve within it. Diagnostics just pass the `Loc`.
- Non-empty symbols should be created through `SymPool::sym` / `Driver::sym`, not by constructing `Sym` manually. Use `SymMap` / `SymSet` aliases instead of concrete hash container types, especially because `FE_ABSL` switches those aliases to Abseil containers.
- Diagnostics are `std::format`-based and go through `fe::Error::{error,warn,note}`. Follow that pattern rather than inventing separate reporting helpers.
- A note attaches to the error or warning that precedes it. `Error::note` without a `Loc` renders as a `= note:` continuation; with a `Loc` it points somewhere else and gets a header line and snippet of its own - and is dropped when that `Loc` overlaps the primary one and thus points nowhere new.
- `Error::report` streams and claims everything, `Error::bail` always throws an `Error::Bail`, and `Error::ack` bails on errors and merely reports warnings. Build and throw one diagnostic in a single expression with `Error(driver).error(...).note(...).bail()`.
- If a type already has `operator<<`, expose it to `std::format` with `template<> struct std::formatter<T> : fe::ostream_formatter {};`.
- Derived lexers/parsers pull the CRTP base helpers they use into scope with `using` declarations (`ahead`, `accept`, `next`, `loc_`, `peek`, `str_` for the lexer; `accept`, `anchor`, `eat`, `expect`, `lex`, `recover`, `tracker` for the parser), matching the pattern in `tests/lexer.cpp`.
- `fe/loc.h` only declares `operator<<` for `Pos` and `Loc`: link `fe-lib` for the default rendering, or define them yourself. The same split applies to `fe::Snippet`, which `fe::Error` puts under every diagnostic.

## Parser contract

`fe::Parser` never reports anything itself; the derived class `S` must provide (and `friend` the base if they are private):

- `Lexer& lexer()` - where `Parser::lex` pulls the next token from.
- `void syntax_err(Tag, std::string_view ctxt)` - `Parser::expect` did not find its `Tag`.
- `void unanchored_err(Tok, std::string_view ctxt)` - `Parser::recover` discarded this token.

Error recovery is anchor-based: an *anchor* is a `Tag` an enclosing context is still waiting for.
`Parser::anchor(tag, ctxt)` returns an RAII `Anchor` that anchors `tag` for the scope and - if given a `ctxt` - `expect`s it at the end of that scope; omit `ctxt` to merely anchor.
`Parser::recover` then discards only tokens that are *not* anchored, so a nested parser bails out instead of swallowing a token its caller needs.
Prefer this over hand-rolled skip loops, and keep `expect`/`anchor` context strings noun phrases ("parenthesized expression"): they end up inside the message `syntax_err` builds.
Both take a `std::format_string` overload; use it instead of formatting the context yourself.

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
