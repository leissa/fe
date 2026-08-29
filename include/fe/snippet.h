#pragma once

#include <cstdint>

#include <iosfwd>

#include "fe/loc.h"
#include "fe/term.h"

namespace fe {

/// The underlined source excerpt that goes below a diagnostic.
/// Streams every source row Snippet::loc spans, each underlining the columns Snippet::loc covers on it.
/// Streams nothing if Snippet::loc has no Loc::src or does not resolve within it.
struct Snippet {
    Loc loc;
    term::FG color    = term::FG::Red;
    uint32_t gutter   = 5; ///< Width of the line-number column.
    uint32_t max_rows = 8; ///< Rows streamed before the middle is elided; `0` elides nothing.

    /// `fe/snippet.h` merely *declares* this.
    /// Link `fe-lib` for the default implementation, or provide your own in namespace `fe`.
    friend std::ostream& operator<<(std::ostream&, const Snippet&);
};

} // namespace fe
