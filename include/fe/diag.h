#pragma once

#include <cstdint>

#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>

#include "fe/api.h"
#include "fe/loc.h"
#include "fe/term.h"

namespace fe {

/// How a diagnostic lays out - and how much of it an Error keeps.
/// The knobs below cover the common adjustments; derive and Driver::diag your subclass for the rest.
/// @warning A subclass that refers back to its Driver does not survive a move; keep the Driver pinned.
class FE_API Diag {
public:
    enum class Tag {
        Error,
        Warn,
        Note,
    };

    Diag()            = default;
    Diag(const Diag&) = delete;
    virtual ~Diag();

    uint32_t gutter      = 5;                ///< Width of the line-number column.
    uint32_t max_rows    = 8;                ///< Rows a snippet streams before eliding its middle; `0` elides nothing.
    uint32_t max_errors  = 0;                ///< Errors recorded before the rest is dropped; `0` keeps everything.
    bool no_snippet      = false;            ///< If `true`, a diagnostic is only its header line.
    bool werror          = false;            ///< If `true`, a warning is recorded as an error.
    Loc::Style loc_style = Loc::Style::Full; ///< How Diag::loc spells out a position.

    /// @name Layout
    /// Each of these streams one piece of a diagnostic; override to lay that piece out differently.
    ///@{
    virtual void loc(std::ostream&, Loc) const;                           ///< Just the position.
    virtual void header(std::ostream&, Loc, Tag, std::string_view) const; ///< The `loc: tag: msg` line.
    virtual void snippet(std::ostream&, Loc, Tag) const;                  ///< The underlined source excerpt.
    virtual void note(std::ostream&, Loc, std::string_view) const;        ///< One note below the message it belongs to.
    virtual void summary(std::ostream&, size_t num_errors, size_t num_warnings, bool truncated) const;
    ///@}

    /// Postprocesses the text of one Error::Msg; the identity here, see CodeDiag.
    /// @warning @p fmt captures its arguments by reference and is only valid for that one call.
    virtual std::string render(const std::function<std::string()>& fmt) const { return fmt(); }

    static term::FG tag2color(Tag);
};

/// The Diag a Driver installs by default: colors a `` `citation` `` and drops its backticks - or keeps them
/// verbatim without color; `` \` `` is a literal backtick.
/// @warning Renders when the message is *recorded*, so Mode::Auto needs term::resolve_mode up front.
class FE_API CodeDiag : public Diag {
public:
    std::string render(const std::function<std::string()>&) const override;
};

FE_API std::ostream& operator<<(std::ostream&, Diag::Tag);

} // namespace fe
