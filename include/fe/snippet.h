#pragma once

#include <cstdint>

#include <iosfwd>

#include "fe/loc.h"
#include "fe/term.h"

namespace fe {

/// Streams every source row @p loc spans, each underlining the columns @p loc covers on it.
/// Streams nothing if @p loc has no Loc::src or does not resolve within it.
/// @p gutter is the width of the line-number column;
/// @p max_rows is the number of rows streamed before the middle is elided - `0` elides nothing.
std::ostream&
stream_snippet(std::ostream& os, Loc loc, term::FG color = term::FG::Red, uint32_t gutter = 5, uint32_t max_rows = 8);

} // namespace fe
