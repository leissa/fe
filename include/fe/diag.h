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
struct Diagnostics {
    virtual ~Diagnostics() = default;

    Diag diag;

    /// Renders the text of one Error::Msg.
    /// The default simply invokes @p fmt; override to postprocess it - or to invoke @p fmt a second time,
    /// e.g. once the first pass turns out to have rendered two distinct entities under the same name.
    /// @warning @p fmt captures its arguments by reference and is only valid for the duration of this call.
    virtual std::string render(const std::function<std::string()>& fmt) const { return fmt(); }
};

} // namespace fe
