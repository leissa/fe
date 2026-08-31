#pragma once

#include <cassert>
#include <concepts>

#include <ostream>
#include <print>
#include <source_location>
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
    enum class Level { Error, Warn, Info, Verbose, Debug, Trace };

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

    /// A std::format_string that remembers where it was written.
    template<class... Args>
    struct FmtLoc {
        template<class S>
        requires std::convertible_to<const S&, std::string_view>
        consteval FmtLoc(const S& fmt, std::source_location loc = std::source_location::current())
            : fmt(fmt)
            , loc(loc) {}

        std::format_string<Args...> fmt;
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
    void log(Level level, Loc loc, std::format_string<Args...> fmt, Args&&... args) const {
        if (ostream_ && level <= max_level_) emit(level, loc, fmt, std::forward<Args>(args)...);
    }

    /// A std::source_location is no Loc: it points into *your* source, which has no fe::Src.
    template<class... Args>
    void log(Level level, std::source_location where, std::format_string<Args...> fmt, Args&&... args) const {
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
    template<class... Args>
    void e(Fmt<Args...> fmt, Args&&... args) const {
        log(Level::Error, fmt, std::forward<Args>(args)...);
    }
    template<class... Args>
    void w(Fmt<Args...> fmt, Args&&... args) const {
        log(Level::Warn, fmt, std::forward<Args>(args)...);
    }
    template<class... Args>
    void i(Fmt<Args...> fmt, Args&&... args) const {
        log(Level::Info, fmt, std::forward<Args>(args)...);
    }
    template<class... Args>
    void v(Fmt<Args...> fmt, Args&&... args) const {
        log(Level::Verbose, fmt, std::forward<Args>(args)...);
    }
    ///@}

    /// @name Debug Shorthands
    /// Vaporize to nothingness in `Release` build; the arguments are still evaluated.
    ///@{
#ifndef NDEBUG
    template<class... Args>
    void d(Fmt<Args...> fmt, Args&&... args) const {
        log(Level::Debug, fmt, std::forward<Args>(args)...);
    }
    template<class... Args>
    void t(Fmt<Args...> fmt, Args&&... args) const {
        log(Level::Trace, fmt, std::forward<Args>(args)...);
    }
#else
    template<class... Args>
    void d(Fmt<Args...>, Args&&...) const {}
    template<class... Args>
    void t(Fmt<Args...>, Args&&...) const {}
#endif
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
            case Level::Trace:   return 'T';
            case Level::Debug:   return 'D';
            case Level::Verbose: return 'V';
            case Level::Info:    return 'I';
            case Level::Warn:    return 'W';
            case Level::Error:   return 'E';
            default: unreachable();
        }
    }

    static term::FG level2color(Level level) {
        switch (level) {
            case Level::Trace:   return term::FG::Magenta;
            case Level::Debug:   return term::FG::Cyan;
            case Level::Verbose: return term::FG::Blue;
            case Level::Info:    return term::FG::Green;
            case Level::Warn:    return term::FG::Yellow;
            case Level::Error:   return term::FG::Red;
            default: unreachable();
        }
    }
    // clang-format on
    ///@}

private:
    template<class W, class... Args>
    void emit(Level level, const W& where, std::format_string<Args...> fmt, Args&&... args) const {
        std::print(ostream(), "{}{}:{}{}:{} ", level2color(level), level2acro(level), term::FG::Gray, where,
                   term::FG::Reset);
        std::println(ostream(), fmt, std::forward<Args>(args)...);
        if ((level == Level::Error && break_on_error) || (level == Level::Warn && break_on_warn)) breakpoint();
    }

    std::ostream* ostream_ = nullptr;
    Level max_level_       = Level::Error;
};

} // namespace fe
