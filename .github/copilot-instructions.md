# FE repository instructions

`fe` is a CMake-based C++ library of reusable building blocks for writing language frontends (arena allocation, string interning, source locations, UTF-8 lexer/parser CRTP bases, diagnostics).
Most of it is **header-only**; `fe-lib` (`FE_LIB=ON`, the default) builds the handful of components that cannot be (see below).
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

CI (`.github/workflows/`) builds one compiler per platform in Debug and Release - gcc-14 on Linux, Apple clang on macOS, MSVC on Windows - and runs `fe-test` under Valgrind as well as ASan/LSan/UBSan.
A change is only done when it is leak- and UB-clean, not merely when `ctest` passes.

## Build options & toolchain

- The library requires **C++23** (`target_compile_features` in `CMakeLists.txt`).
- `FE_LIB` (default `ON`): builds `fe-lib`, an `OBJECT` library over `src/fe/`. `OFF` keeps only the header-only building blocks; `fe::Driver`, `fe::Error`, and everything else listed under `fe-lib` below is then unavailable.
- `FE_ABSL` (default `OFF`): switches `SymMap`/`SymSet`/`PathMap` and friends from `std` to Abseil containers.
- `FE_BUILD_DOCS` (default `OFF`): build Doxygen docs (requires Doxygen + Graphviz `dot`).
- `BUILD_TESTING` (CTest default `ON`): builds the only executable, `fe-test`.
- MSVC: `CMakeLists.txt` adds `/utf-8 /wd4146 /wd4245` and `_CTYPE_DISABLE_MACROS`. Keep new headers MSVC-clean; UTF-8 source handling is assumed.

## High-level architecture

The public API lives entirely in `include/fe/`. `fe` is always an `INTERFACE` target carrying the header usage requirements; `FE_LIB` adds `fe-lib`, an `OBJECT` library over `src/fe/` that links `fe` publicly. Tests build the only executable (`fe-test`).

The library is organized around a few reusable frontend-building blocks that are designed to be composed:

- `fe::Arena` (`arena.h`) provides arena allocation, an STL allocator adapter, and arena-backed `unique_ptr` support for AST-style ownership.
- `fe::Sym` and `fe::SymPool` (`sym.h`) intern strings so identifiers can be compared cheaply by pointer after interning.
- `fe::Driver` (`driver.h`) is the shared frontend context: it inherits `SymPool`, owns the `SrcMap` (`Driver::src`), the interned `Dbg`s (`Driver::dbg`), the `fe::Diag` (`Driver::diag`), and the `fe::Error` every building block reports into (`Driver::error`, plus `error`/`warn`/`note` shorthands).
  It is not movable: the `Error` - and a `Diag` of your own - point back at it.
- `fe::Diag` (`diag.h`) owns how a diagnostic lays out: the knobs (`gutter`, `max_rows`, `max_errors`, `no_snippet`, `werror`, `loc_style` of type `Loc::Style`) plus one virtual per piece (`loc`, `header`, `snippet`, `note`, `summary`) and the `render` hook each message is formatted through.
  Adjust the knobs for the common cases; derive and `Driver::diag(std::make_unique<MyDiag>())` to lay a diagnostic out from scratch.
- `fe::Error` (`error.h`) collects `std::format`-based diagnostics - errors and warnings, each owning the notes that hang off it - and renders each with the `Snippet` its `Loc` points at.
  It is a sink, not an exception: `Error::ack`/`Error::bail` render everything and throw the text as an `Error::Bail`, which no longer points into any `Src` and may propagate past the `Driver`.
  Report into `Driver::error`; construct one of your own only for a diagnostic that must not join that sink.
- `fe::Pos` and `fe::Loc` (`loc.h`) track source positions/locations and are threaded through lexers, parsers, and diagnostics.
- `fe::Src` and `fe::SrcMap` (`src.h`) own the text of each source file and turn a `Pos` back into a row/column. A `Loc` borrows a `const Src*`, which is how it renders itself as `path:row:col`.
- `fe::Ring` (`ring.h`) is the fixed-size lookahead buffer used by the lexer/parser blueprints.
- `fe::Worklist` (`worklist.h`) is a queue/stack that pushes each element at most once; use it through the `BFSWorklist`/`DFSWorklist` aliases.
- `fe::Bitset` (`bitset.h`) is a dynamically growing set of bit indices with small storage optimization: the first 64 bits live inside the `Bitset` itself, so small sets never allocate.
- `fe::XTrie` (`xtrie.h`) hash-conses sets of pointers - small ones as sorted arrays, large ones as paths in an [IndexedTrie](https://dl.acm.org/doi/10.1145/3808286) built on the intrusive link-cut-tree of `lct.h` - so that equal sets are pointer-equal.
  A *key* trait passed as template argument tells it how to read the `gid`/`tid` of an element.
- `fe::Lexer<K, S>` (`lexer.h`) is a CRTP base that handles UTF-8 decoding, character lookahead, token text accumulation (`str_`), source location tracking (`loc_`), the `recover_utf8`/`recover_char` skips below, and default `utf8_err`/`char_err` diagnostics.
- `fe::Parser<Tok, Tag, K, S>` (`parser.h`) is a CRTP base that wraps a lexer with token lookahead, `accept`/`expect`/`eat`, `Tracker` helpers for building node spans, the anchor-based error recovery described below, and default `syntax_err`/`unanchored_err` diagnostics.

Support headers: `algo.h` (bit casts, padding, small string/range algorithms), `assert.h` (`assert`/`assertf`/`unreachable`), `cast.h` (checked/dynamic casts), `cli.h` (`fe::cli`, a single-command `argc`/`argv` parser that renders its help for a terminal or as Markdown tables; needs `fe-lib`), `container.h` (`pop`/`lookup` helpers, `Stacklike`/`Queuelike` concepts), `dbg.h` (`fe::Dbg`, a `Loc`/`Sym` pair), `enum.h` (bit-flag enum ops), `format.h` (`ostream_formatter`, `std::format` glue), `hash.h` (`constexpr` hash mixing/combining), `log.h` (`fe::Log`, leveled logging; its `error`/`warn`/... shorthands capture the call site with `std::source_location`), `restore.h` (`fe::Restore`, an RAII guard that restores a reference - or a getter/setter pair - at end of scope), `span.h` (`fe::Span`/`fe::View`), `term.h` (terminal/ANSI color, incl. `fe::term::ScopedMode`), `utf8.h` (UTF-8 decode primitives), `vector.h` (`fe::Vector`, small-buffer vector), `worklist.h` (`fe::Worklist` and its `BFSWorklist`/`DFSWorklist` aliases).

`fe-lib` holds the components that need a translation unit of their own: `fe::Diag` (`diag.h`), the `fe::Driver` ctor/dtor that owns one, `Error::diag` (`error.h`, which only forward-declares the `Driver` it reads the `Diag` from), the default `Pos`/`Loc` streaming and `dump` (`loc.h`), `fe::Snippet` (`snippet.h`), `Cli::parse`/`Cli::help`/`Cli::markdown` (`cli.h`, the only part of `fe::cli` that is not header-only), `fe::dl` (`dl.h`, dynamic library loading), `fe::sys` (`sys.h`, locating and running external commands), and `fe::Profiler` (`profile.h`, nested wall-clock spans reported as a flat table, a tree, or Chrome Trace JSON).
It is an `OBJECT` library on purpose: link it into exactly one shared library of yours and every other consumer resolves those symbols there instead of carrying a copy.

`tests/lexer.cpp` is the best end-to-end example of intended use: define a token type with `tag()` and `loc()`, derive a concrete lexer/parser from the CRTP bases, use `fe::Driver` for identifier interning and diagnostics, and let locations flow through tokens for error reporting.

## Key conventions

- Keep library code header-only unless it genuinely cannot be; then declare it in `include/fe/` and implement it in `src/fe/`, which is what `fe-lib` compiles. Public headers are listed explicitly in `CMakeLists.txt` and installed from `include/fe/`.
- Default-constructed values are meaningful sentinels across the API: `Tok{}` means parse failure, `Sym{}` is the empty symbol, and default `Pos`/`Loc` are invalid. `Parser::accept` and `Parser::expect` rely on this pattern.
- `Loc::end` is **exclusive** (the byte one past the span), just like an STL iterator. `Loc::src` is a borrowed `const Src*`, so the `Src` must outlive the `Loc`; a `SrcMap` owns one for you.
- `Loc` is kept at two machine words (`static_assert` in `loc.h`) so it stays a value passed in registers - do not grow it.
- `SrcMap` interns paths under `SrcMap::key` (absolute, symlink-free, normalized), so one file yields exactly one `Src`. That is what lets `Loc` compare files by pointer - do not hand a `Loc` a `Src` that some other `SrcMap` (or nobody) owns.
- A `Loc` renders itself: `operator<<`/`std::format` spell out `path:row:col-row:col` via `Loc::src`, falling back to `path@begin-end` when it has no `Src` or the offsets do not resolve within it. Diagnostics just pass the `Loc`.
- Non-empty symbols should be created through `SymPool::sym` / `Driver::sym`, not by constructing `Sym` manually. Use `SymMap` / `SymSet` aliases instead of concrete hash container types, especially because `FE_ABSL` switches those aliases to Abseil containers.
- Diagnostics are `std::format`-based and go through `fe::Error::{error,warn,note}` - normally reached as `Driver::{error,warn,note}`. Follow that pattern rather than inventing separate reporting helpers.
- Put a `` `citation` `` in backticks: `fe::Diag` colors what they enclose and drops them, or keeps them verbatim without color. Escape a literal one as `` \` ``.
- A note attaches to the error or warning that precedes it. `Error::note` without a `Loc` renders as a `= note:` continuation; with a `Loc` it points somewhere else and gets a header line and snippet of its own - and is dropped when that `Loc` overlaps the primary one and thus points nowhere new.
- `Error::report` streams and claims everything, `Error::bail` always throws an `Error::Bail`, and `Error::ack` bails on errors and merely reports warnings. Build and throw one diagnostic in a single expression with `Error(driver).error(...).note(...).bail()`.
- Keep `Driver` free of virtual functions; `Driver::diag` is where a consumer plugs in behavior of its own.
- A vtable is a *data* symbol, and Windows resolves one exported from a shared library only through `__declspec(dllimport)` - without it the linker silently binds it to a call thunk and the first virtual dispatch jumps into hyperspace. Hence `FE_API` on `fe::Diag` - `generate_export_header` generates that macro into `fe/api.h` - and hence the `Driver` ctor lives in `fe-lib`: nothing must emit `fe::Diag`'s vtable into a consumer's shared library. Annotate any further polymorphic type on that boundary the same way.
- If a type already has `operator<<`, expose it to `std::format` with `template<> struct std::formatter<T> : fe::ostream_formatter {};`.
- Derived lexers/parsers pull the CRTP base helpers they use into scope with `using` declarations (`ahead`, `accept`, `next`, `recover_char`, `recover_utf8`, `loc_`, `peek`, `str_` for the lexer; `accept`, `anchor`, `eat`, `expect`, `lex`, `recover`, `tracker` for the parser), matching the pattern in `tests/lexer.cpp`.
- `fe/loc.h` only declares `operator<<` for `Pos` and `Loc`: link `fe-lib` for the default rendering, or define them yourself. The same split applies to `fe::Snippet`, which `fe::Diag` puts under every diagnostic.

## Lexer contract

The derived class `S` must provide `fe::Driver& driver()` (and `friend` the base if it is private) - the default diagnostics go to its `Driver::error`.

Both come with a default implementation that `S` may replace with one of its own:

- `void utf8_err()` - `Lexer::recover_utf8` discarded the malformed bytes at `loc_`.
- `void char_err(char32_t)` - `Lexer::recover_char` discarded that character at `loc_`.

Recovery is a skip: `recover_utf8` swallows a whole run of malformed UTF-8 and reports it once - check it *before* your token dispatch, since `utf8::Invalid` is no code point and matches no rule of yours - and `recover_char` swallows the one character nothing else matched, so it belongs at the very end of your dispatch (never at `utf8::EoF`, or the lexer spins).
Both want `Lexer::start` to have run, so `loc_` spans exactly what was discarded.

## Parser contract

The derived class `S` must provide (and `friend` the base if they are private):

- `Lexer& lexer()` - where `Parser::lex` pulls the next token from.
- `fe::Driver& driver()` - the default diagnostics go to its `Driver::error`.

Both diagnostics come with a default implementation that `S` may replace with one of its own:

- `void syntax_err(std::string_view what, Tok, std::string_view ctxt)` - `what` was expected but that `Tok` showed up.
  The `(std::string_view what, std::string_view ctxt)` and `(Tag, std::string_view ctxt)` overloads funnel through it, so overriding that one suffices.
- `void unanchored_err(Tok, std::string_view ctxt)` - `Parser::recover` discarded this token.

The Parser dispatches through `S`, so a declaration there wins - but it hides *all* base overloads of that name, so add `using Super::syntax_err;`.

Error recovery is anchor-based: an *anchor* is a `Tag` an enclosing context is still waiting for.
`Parser::anchor(tag)` returns an RAII `Anchor` that anchors `tag` for the scope; `expect` it yourself at the end of that scope.
`Parser::recover` then discards only tokens that are *not* anchored, so a nested parser bails out instead of swallowing a token its caller needs.
Prefer this over hand-rolled skip loops, and keep `expect` context strings noun phrases ("parenthesized expression"): they end up inside the message `syntax_err` builds.
`expect` takes a `std::format_string` overload; use it instead of formatting the context yourself.

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
