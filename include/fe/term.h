#pragma once

#include <cstdlib>
#include <cstring>

#include <atomic>
#include <iostream>
#include <ostream>
#include <string_view>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <unistd.h>
#endif

#include "fe/assert.h"
#include "fe/format.h"

/// Lightweight stream-based terminal colors for diagnostics and CLI output.
///
/// Include `fe/term.h` and stream a @ref fe::term::FG value into an `std::ostream`:
/// ```
/// std::cerr << fe::term::FG::Red << "error: " << fe::term::FG::Reset << "unexpected token\n";
/// ```
///
/// The current behavior is controlled via @ref fe::term::Mode and can be overridden with
/// @ref fe::term::set_mode. In @ref fe::term::Mode::Auto, colors are emitted only for
/// `std::cout`, `std::cerr`, `std::clog`, or streams sharing those buffers when they refer to
/// terminals. FE also respects the common environment conventions `NO_COLOR`, `CLICOLOR=0`, and
/// `CLICOLOR_FORCE` (unless it is set to `0`).
namespace fe::term {

/// Controls whether color escape sequences are emitted.
enum class Mode {
    Auto,
    Never,
    Always,
};

/// Foreground colors that can be streamed into an `std::ostream`.
enum class FG {
    Black,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    Gray,
    Grey = Gray,
    Reset,
};

namespace detail {

enum class Stream {
    Unknown,
    Stdout,
    Stderr,
};

inline bool env_set(const char* name) {
    auto* value = std::getenv(name);
    return value && *value != '\0';
}

inline bool env_is(const char* name, const char* expected) {
    auto* value = std::getenv(name);
    return value && std::strcmp(value, expected) == 0;
}

inline Mode default_mode() {
    if (env_set("NO_COLOR")) return Mode::Never;
    if (env_set("CLICOLOR_FORCE") && !env_is("CLICOLOR_FORCE", "0")) return Mode::Always;
    if (env_is("CLICOLOR", "0")) return Mode::Never;
    return Mode::Auto;
}

inline std::atomic<Mode>& current_mode() {
    static std::atomic<Mode> mode(default_mode());
    return mode;
}

inline std::streambuf* stdout_rdbuf() {
    static std::streambuf* buf = std::cout.rdbuf();
    return buf;
}

inline std::streambuf* stderr_rdbuf() {
    static std::streambuf* buf = std::cerr.rdbuf();
    return buf;
}

inline std::streambuf* clog_rdbuf() {
    static std::streambuf* buf = std::clog.rdbuf();
    return buf;
}

inline Stream stream(std::ostream& os) {
    auto* const buf = os.rdbuf();
    if (buf == stdout_rdbuf()) return Stream::Stdout;
    if (buf == stderr_rdbuf() || buf == clog_rdbuf()) return Stream::Stderr;
    return Stream::Unknown;
}

#ifdef _WIN32
inline bool enable_vt(HANDLE handle) {
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return false;
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

inline bool is_terminal(Stream stream) {
    switch (stream) {
        case Stream::Stdout: {
            static bool stdout_is_terminal = enable_vt(GetStdHandle(STD_OUTPUT_HANDLE));
            return stdout_is_terminal;
        }
        case Stream::Stderr: {
            static bool stderr_is_terminal = enable_vt(GetStdHandle(STD_ERROR_HANDLE));
            return stderr_is_terminal;
        }
        default: return false;
    }
}
#else
inline bool is_terminal(Stream stream) {
    switch (stream) {
        case Stream::Stdout: {
            static bool stdout_is_terminal = ::isatty(STDOUT_FILENO) != 0;
            return stdout_is_terminal;
        }
        case Stream::Stderr: {
            static bool stderr_is_terminal = ::isatty(STDERR_FILENO) != 0;
            return stderr_is_terminal;
        }
        default: return false;
    }
}
#endif

inline bool use_color(std::ostream& os) {
    // clang-format off
    switch (current_mode().load(std::memory_order_relaxed)) {
        case Mode::Always: return true;
        case Mode::Never:  return false;
        case Mode::Auto:   return is_terminal(stream(os));
        default: fe::unreachable();
    }
    // clang-format on
}

inline std::string_view sgr(FG color) {
    // clang-format off
    switch (color) {
        case FG::Black:   return "\033[30m";
        case FG::Red:     return "\033[31m";
        case FG::Green:   return "\033[32m";
        case FG::Yellow:  return "\033[33m";
        case FG::Blue:    return "\033[34m";
        case FG::Magenta: return "\033[35m";
        case FG::Cyan:    return "\033[36m";
        case FG::Gray:    return "\033[90m";
        case FG::Reset:   return "\033[39m";
        default: fe::unreachable();
    }
    // clang-format on
}

} // namespace detail

/// Returns the current terminal color mode.
inline Mode mode() { return detail::current_mode().load(std::memory_order_relaxed); }

/// Overrides the current terminal color mode.
inline void set_mode(Mode mode) { detail::current_mode().store(mode, std::memory_order_relaxed); }

/// Streams the ANSI escape sequence for @p color when colors are enabled for @p os.
inline std::ostream& operator<<(std::ostream& os, FG color) {
    if (detail::use_color(os)) os << detail::sgr(color);
    return os;
}

} // namespace fe::term

#ifndef DOXYGEN
template<> struct std::formatter<fe::term::FG> : fe::ostream_formatter {};
#endif
