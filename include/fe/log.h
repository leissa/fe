#pragma once

#include <cassert>
#include <concepts>

#include <ostream>
#include <print>
#include <source_location>
#include <sstream>
#include <string_view>
#include <type_traits>

#include "fe/assert.h"
#include "fe/format.h"
#include "fe/loc.h"
#include "fe/term.h"

namespace fe {

/// Facility to log what you are doing.
class Log {
public:
    enum class Level {
        Error,
        Warn,
        Info,
        Verbose,
        Debug,
        Trace,
        E = Error,
        W = Warn,
        I = Info,
        V = Verbose,
        D = Debug,
        T = Trace,
    };

    /// @name Getters
    ///@{
    Level level() const { return max_level_; }
    std::ostream& ostream() const {
        assert(ostream_);
        return *ostream_;
    }
    explicit operator bool() const { return ostream_; } ///< Checks if Log::ostream_ is set.
    ///@}

    /// @name Setters
    ///@{
    Log& set(std::ostream* ostream) {
        ostream_ = ostream;
        return *this;
    }
    Log& set(Level max_level) {
        max_level_ = max_level;
        return *this;
    }
    ///@}

    /// A cite_string that remembers where it was written.
    template<class... Args>
    struct FmtLoc {
        template<class S>
        requires std::convertible_to<const S&, std::string_view>
        consteval FmtLoc(const S& fmt, std::source_location loc = std::source_location::current())
            : fmt(fmt)
            , loc(loc) {}

        cite_string<Args...> fmt;
        std::source_location loc;
    };

    /// Puts the `Args` of a Log::FmtLoc parameter into a non-deduced context, so that they are deduced from the
    /// trailing arguments and the format string keeps capturing its call site.
    template<class... Args>
    using Fmt = FmtLoc<std::type_identity_t<Args>...>;

    /// @name Log
    /// Output @p fmt to Log::ostream; does nothing if Log::ostream is `nullptr`.
    ///@{
    template<class... Args>
    void log(Level level, Loc loc, cite_string<Args...> fmt, Args&&... args) const {
        if (ostream_ && level <= max_level_) emit(level, loc, fmt, std::forward<Args>(args)...);
    }

    /// A std::source_location is no Loc: it points into *your* source, which has no fe::Src.
    template<class... Args>
    void log(Level level, std::source_location where, cite_string<Args...> fmt, Args&&... args) const {
        if (ostream_ && level <= max_level_)
            emit(level, std::format("{}:{}", where.file_name(), where.line()), fmt, std::forward<Args>(args)...);
    }

    /// Points at the call site.
    template<class... Args>
    void log(Level level, Fmt<Args...> fmt, Args&&... args) const {
        log(level, fmt.loc, fmt.fmt, std::forward<Args>(args)...);
    }
    ///@}

    /// @name Level Shorthands
    /// Log at a fixed Level, pointing at the call site.
    ///@{
    // clang-format off
    template<class... Args> void e(Fmt<Args...> fmt, Args&&... args) const { log(Level::E, fmt, std::forward<Args>(args)...); }
    template<class... Args> void w(Fmt<Args...> fmt, Args&&... args) const { log(Level::W, fmt, std::forward<Args>(args)...); }
    template<class... Args> void i(Fmt<Args...> fmt, Args&&... args) const { log(Level::I, fmt, std::forward<Args>(args)...); }
    template<class... Args> void v(Fmt<Args...> fmt, Args&&... args) const { log(Level::V, fmt, std::forward<Args>(args)...); }
    // clang-format on
    ///@}

    /// @name Debug Shorthands
    /// Vaporize to nothingness in `Release` build; the arguments are still evaluated.
    ///@{
#ifndef NDEBUG
    // clang-format off
    template<class... Args> void d(Fmt<Args...> fmt, Args&&... args) const { log(Level::Debug, fmt, std::forward<Args>(args)...); }
    template<class... Args> void t(Fmt<Args...> fmt, Args&&... args) const { log(Level::Trace, fmt, std::forward<Args>(args)...); }
#else
    template<class... Args> void d(Fmt<Args...>, Args&&...) const {}
    template<class... Args> void t(Fmt<Args...>, Args&&...) const {}
#endif
    // clang-format on
    ///@}

    /// @name Breakpoints
    ///@{
    bool break_on_error = false;
    bool break_on_warn  = false;
    ///@}

    /// @name Conversions
    ///@{
    // clang-format off
    static char level2acro(Level level) {
        switch (level) {
            case Level::T: return 'T';
            case Level::D: return 'D';
            case Level::V: return 'V';
            case Level::I: return 'I';
            case Level::W: return 'W';
            case Level::E: return 'E';
            default: unreachable();
        }
    }

    static term::FG level2color(Level level) {
        switch (level) {
            case Level::T: return term::FG::Magenta;
            case Level::D: return term::FG::Cyan;
            case Level::V: return term::FG::Blue;
            case Level::I: return term::FG::Green;
            case Level::W: return term::FG::Yellow;
            case Level::E: return term::FG::Red;
            default: unreachable();
        }
    }
    // clang-format on
    ///@}

private:
    /// Renders into a detached buffer, so a `` `citation` `` follows the same Mode::Auto decision the
    /// term::FG values of the prefix do; see fe/term.h.
    template<class W, class... Args>
    void emit(Level level, const W& where, cite_string<Args...> fmt, Args&&... args) const {
        auto oss = std::ostringstream();
        term::render_cite(oss, term::detail::vformat_cite(fmt.get(), args...));
        std::println(ostream(), "{}{}:{}{}:{} {}", level2color(level), level2acro(level), term::FG::Gray, where,
                     term::FG::Reset, oss.str());
        if ((level == Level::E && break_on_error) || (level == Level::W && break_on_warn)) breakpoint();
    }

    std::ostream* ostream_ = nullptr;
    Level max_level_       = Level::E;
};

} // namespace fe
