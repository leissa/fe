#pragma once

#include <cstdint>

#include <functional>
#include <string>

namespace fe {

/// How an Error lays out - and how much of it it keeps.
struct Diag {
    uint32_t gutter     = 5;     ///< Width of the line-number column.
    uint32_t max_rows   = 8;     ///< Rows a Snippet streams before eliding its middle; `0` elides nothing.
    uint32_t max_errors = 0;     ///< Errors recorded before the rest is dropped; `0` keeps everything.
    bool no_snippet     = false; ///< If `true`, a diagnostic is only its header line.
    bool werror         = false; ///< If `true`, a warning is recorded as an error.
};

/// The diagnostic policy an Error works against.
/// fe::Driver is one - but an Error does not need a whole Driver.
/// @note Deliberately free of virtual functions: a polymorphic Driver drags a vtable across every
/// shared-library boundary it is used over, and an exported vtable is a data symbol that Windows only
/// resolves correctly with `__declspec(dllimport)`.
struct Diagnostics {
    /// Renders the text of one Error::Msg; a formatter is handed in and its result returned.
    /// Leave unset to simply invoke it; set it to postprocess the result - or to invoke it a second time,
    /// e.g. once the first pass turns out to have rendered two distinct entities under the same name.
    /// @warning The formatter captures its arguments by reference and is only valid for that one call.
    /// @warning A hook that captures its Diagnostics does not survive a move; keep the owner pinned.
    using Render = std::function<std::string(const std::function<std::string()>&)>;

    Diag diag;
    Render render;
};

} // namespace fe
