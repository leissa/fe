#pragma once

#include <cassert>
#include <cstdint>

#include <ostream>
#include <print>

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

    /// @name Log
    /// Output @p fmt to Log::ostream; does nothing if Log::ostream is `nullptr`.
    ///@{
    template<class... Args>
    void log(Level level, Loc loc, std::format_string<Args...> fmt, Args&&... args) const {
        if (ostream_ && level <= max_level_) emit(level, loc, fmt, std::forward<Args>(args)...);
    }

    /// A `__FILE__`/`__LINE__` pair is no Loc: it points into *your* source, which has no fe::Src.
    template<class... Args>
    void log(Level level, const char* file, uint32_t line, std::format_string<Args...> fmt, Args&&... args) const {
        if (ostream_ && level <= max_level_)
            emit(level, std::format("{}:{}", file, line), fmt, std::forward<Args>(args)...);
    }
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
