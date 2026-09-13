#pragma once

#include <iostream>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <sys/ioctl.h>
#    include <unistd.h>
#endif

#include "fe/api.h"
#include "fe/assert.h"
#include "fe/format.h"
#include "fe/restore.h"

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
///
/// Note that a `std::formatter` never sees the destination stream - it formats into a detached buffer.
/// Hence, embedding a @ref fe::term::FG value in a `std::format`/`std::print` format string resolves
/// @ref fe::term::Mode::Auto to "no color".
/// If you emit colors this way, call @ref fe::term::resolve_mode once at startup to decide
/// @ref fe::term::Mode::Auto up front based on a representative stream.
///
/// The mode and @ref fe::term::auto_detached live in the fe library rather than in this header, so a
/// shared library loaded via fe::dl sees whatever the host set instead of starting over from the defaults.
///
/// Use @ref fe::term::use_color to branch on whether color will actually be emitted, e.g. to keep a
/// plain-text fallback in sync with the colored rendering.
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

inline std::streambuf* stdout_rdbuf() noexcept {
    static std::streambuf* buf = std::cout.rdbuf();
    return buf;
}

inline std::streambuf* stderr_rdbuf() noexcept {
    static std::streambuf* buf = std::cerr.rdbuf();
    return buf;
}

inline std::streambuf* clog_rdbuf() noexcept {
    static std::streambuf* buf = std::clog.rdbuf();
    return buf;
}

inline Stream stream(std::ostream& os) noexcept {
    auto* const buf = os.rdbuf();
    if (buf == stdout_rdbuf()) return Stream::Stdout;
    if (buf == stderr_rdbuf() || buf == clog_rdbuf()) return Stream::Stderr;
    return Stream::Unknown;
}

#ifdef _WIN32
inline bool enable_vt(HANDLE handle) noexcept {
    if (handle == INVALID_HANDLE_VALUE) return false;

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return false;
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

inline bool is_terminal(Stream s) noexcept {
    switch (s) {
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
inline bool is_terminal(Stream s) noexcept {
    switch (s) {
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

constexpr std::string_view sgr(FG color) noexcept {
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

/// Does @p str spell an escape - `` \` `` or `\\` - at position @p i of `[i, end)`?
constexpr bool escape(std::string_view str, size_t i, size_t end) noexcept {
    return str[i] == '\\' && i + 1 != end && (str[i + 1] == '`' || str[i + 1] == '\\');
}

/// Index of the next backtick at or after @p i that is not escaped, or `npos`.
inline size_t tick(std::string_view str, size_t i) noexcept {
    for (; i != str.size(); ++i)
        if (escape(str, i, str.size()))
            ++i;
        else if (str[i] == '`')
            return i;
    return std::string_view::npos;
}

/// Streams `[begin, end)` of @p str, dropping the leading backslash of every escape.
inline void stream_raw(std::ostream& os, std::string_view str, size_t begin, size_t end) {
    for (auto i = begin; i != end; ++i) {
        if (escape(str, i, end)) ++i;
        os << str[i];
    }
}

/// Columns `[begin, end)` of @p str occupies once streamed via stream_raw - one less per escape.
inline size_t raw_width(std::string_view str, size_t begin, size_t end) noexcept {
    size_t width = 0;
    for (auto i = begin; i != end; ++i, ++width)
        if (escape(str, i, end)) ++i;
    return width;
}

/// Splits @p str into its `` `citation` `` markup and invokes `f(begin, end, cited)` on each piece;
/// an unpaired backtick is no citation. This is the one place that knows the grammar.
template<class F>
void scan_cite(std::string_view str, F&& f) {
    for (size_t i = 0, e = str.size(); i != e;) {
        auto l = tick(str, i);
        auto r = l == std::string_view::npos ? l : tick(str, l + 1);
        if (r == std::string_view::npos) return f(i, e, false);

        f(i, l, false);
        f(l + 1, r, true);
        i = r + 1;
    }
}

} // namespace detail

/// Returns the current terminal color mode.
FE_API Mode mode() noexcept;

/// Whether Mode::Auto emits colors into a detached buffer - anything a `std::formatter` writes into.
/// @ref resolve_mode is the usual way to decide this.
FE_API bool auto_detached() noexcept;

/// Overrides @ref auto_detached.
FE_API void set_auto_detached(bool b) noexcept;

/// Whether color escape sequences are emitted for @p os right now.
/// In Mode::Auto this is decided by whether @p os refers to a terminal, while a detached buffer
/// (anything a `std::formatter` writes into) follows @ref resolve_mode.
/// Use this to keep a plain-text fallback in sync with what @ref operator<<(std::ostream&, FG) will emit,
/// e.g. to spell out a marker only when it cannot be conveyed by color.
inline bool use_color(std::ostream& os) noexcept {
    auto s = detail::stream(os);
    // clang-format off
    switch (mode()) {
        case Mode::Always: return true;
        case Mode::Never:  return false;
        case Mode::Auto:   return s == detail::Stream::Unknown ? auto_detached() : detail::is_terminal(s);
        default: fe::unreachable();
    }
    // clang-format on
}

/// Number of columns of the terminal @p os refers to.
/// Unlike @ref use_color, this ignores @ref Mode and always asks the actual stream.
/// @returns `std::nullopt` if @p os is not a terminal or its size cannot be determined.
inline std::optional<size_t> width(std::ostream& os) noexcept {
    auto s = detail::stream(os);
    if (!detail::is_terminal(s)) return {};

#ifdef _WIN32
    auto handle = GetStdHandle(s == detail::Stream::Stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    if (CONSOLE_SCREEN_BUFFER_INFO info; GetConsoleScreenBufferInfo(handle, &info)) {
        if (auto cols = info.srWindow.Right - info.srWindow.Left + 1; cols > 0) return size_t(cols);
    }
#else
    auto fd = s == detail::Stream::Stdout ? STDOUT_FILENO : STDERR_FILENO;
    if (winsize ws; ::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return size_t(ws.ws_col);
#endif
    return {};
}

/// Overrides the current terminal color mode.
FE_API void set_mode(Mode m) noexcept;

/// Overrides the color mode for the duration of the scope.
/// @warning The mode is global, so this affects every stream - and every thread - while it is alive.
using ScopedMode = Restore<Mode, &mode, &set_mode>;

/// Decides Mode::Auto for detached buffers, depending on whether @p os refers to a terminal.
/// A `std::formatter` cannot see its destination stream, so FG values embedded in a
/// `std::format`/`std::print` format string never detect a terminal on their own.
/// Call this once at startup to make formatted output colored as well:
/// ```
/// fe::term::resolve_mode(); // decide based on stderr
/// std::print(std::cerr, "{}error:{} ...", fe::term::FG::Red, fe::term::FG::Reset);
/// ```
/// A real stream still decides on its own, and explicit modes are left untouched.
inline void resolve_mode(std::ostream& os = std::cerr) noexcept {
    set_auto_detached(detail::is_terminal(detail::stream(os)));
}

/// Streams the ANSI escape sequence for @p color when colors are enabled for @p os.
inline std::ostream& operator<<(std::ostream& os, FG color) {
    if (use_color(os)) {
        auto esc = detail::sgr(color);
        os.write(esc.data(), esc.size());
    }
    return os;
}

/// Escapes @p str into @p out so that render_cite reproduces it verbatim instead of reading it as markup.
/// Escaping the backslashes as well keeps a trailing one from swallowing the backtick that follows it.
template<class O>
O escape_cite_to(O out, std::string_view str) {
    for (auto c : str) {
        if (c == '`' || c == '\\') *out++ = '\\';
        *out++ = c;
    }
    return out;
}

/// As above but into a fresh `std::string`.
inline std::string escape_cite(std::string_view str) {
    auto res = std::string();
    res.reserve(str.size());
    escape_cite_to(std::back_inserter(res), str);
    return res;
}

/// An *owning* message fragment that spells `` `citations` `` of its own; what format_cite yields.
/// Return this from a helper that assembles a fragment, and it stays valid wherever a Cite may go.
class Cited {
public:
    explicit Cited(std::string str) noexcept
        : str_(std::move(str)) {}

    [[nodiscard]] constexpr std::string_view view() const noexcept { return str_; }
    constexpr operator std::string_view() const noexcept { return str_; }

private:
    std::string str_;
};

/// A *borrowed* fragment whose backticks stay markup; format_cite escapes every other argument.
/// A literal and a Cited convert implicitly - both are text someone wrote as markup.
/// Runtime text has to say so: spell it `Cite(s)`, so data never becomes markup by accident.
/// A `const char*` - a ternary of two literals, say - counts as runtime text and needs `Cite(s)` too.
/// @warning Borrows its text like a `std::string_view` does - a Cited outlives the expression, a Cite does not.
class Cite {
public:
    constexpr Cite() noexcept = default; ///< The empty fragment - a context that says nothing.
    template<size_t N>
    constexpr Cite(const char (&s)[N]) noexcept
        : str_(s) {}
    constexpr Cite(const Cited& cited) noexcept
        : str_(cited) {}
    constexpr explicit Cite(std::string_view s) noexcept
        : str_(s) {}

    [[nodiscard]] constexpr std::string_view view() const noexcept { return str_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return str_.empty(); }
    constexpr explicit operator bool() const noexcept { return !str_.empty(); } ///< Is not empty?

private:
    std::string_view str_;
};

namespace detail {

/// Wraps an argument whose backticks are data and must not delimit a citation.
template<class T>
struct Escaped {
    const T& val;
};

template<class T, class U = std::remove_cvref_t<T>>
using cite_arg_t = std::conditional_t<std::is_same_v<U, Cite> || std::is_same_v<U, Cited>, Cite, Escaped<U>>;

/// std::vformat, but each argument renders as data instead of as markup; see Cite for the exception.
/// @note The wrappers are lambda *parameters* because `std::make_format_args` does not bind rvalues.
template<class... Args>
std::string vformat_cite(std::string_view fmt, const Args&... args) {
    auto vformat = [fmt]<class... A>(A... a) { return std::vformat(fmt, std::make_format_args(a...)); };
    return vformat(cite_arg_t<Args>{args}...);
}

} // namespace detail

/// A std::format_string whose backticks delimit a `` `citation` `` while those of its arguments are data.
template<class... Args>
using cite_string = std::format_string<detail::cite_arg_t<Args>...>;

/// std::format for a message that follows that convention; assemble a fragment of your own with it.
/// The Cited it yields is markup wherever it is used as an argument again - no re-wrapping needed.
template<class... Args>
Cited format_cite(cite_string<Args...> fmt, Args&&... args) {
    return Cited(detail::vformat_cite(fmt.get(), args...));
}

/// Streams @p str into @p os, coloring each `` `citation` `` and dropping its backticks - or keeping them
/// verbatim without color; `` \` `` is a literal backtick and `\\` a literal backslash. This is the convention
/// fe::CodeDiag renders a diagnostic message with; use it to apply the same convention elsewhere, e.g.
/// fe::Cli::help.
inline void render_cite(std::ostream& os, std::string_view str, bool color) {
    // Written out instead of streaming an FG: @p color has already decided, whereas operator<< would
    // ask @p os again - and a detached buffer answers differently than the stream it ends up on.
    auto open  = color ? detail::sgr(FG::Cyan) : std::string_view("`");
    auto close = color ? detail::sgr(FG::Reset) : std::string_view("`");

    detail::scan_cite(str, [&](size_t begin, size_t end, bool cited) {
        if (cited) os << open;
        detail::stream_raw(os, str, begin, end);
        if (cited) os << close;
    });
}

/// As above but lets @p os decide the coloring; mirrors cite_width.
inline void render_cite(std::ostream& os, std::string_view str) { render_cite(os, str, use_color(os)); }

/// Number of columns @p str actually occupies once render_cite renders it with @p color - fewer than
/// `str.size()` by the backticks/backslashes render_cite drops.
inline size_t cite_width(std::string_view str, bool color) {
    size_t width = 0;
    detail::scan_cite(str, [&](size_t begin, size_t end, bool cited) {
        width += detail::raw_width(str, begin, end) + (cited && !color ? 2 : 0); // the backticks stay
    });
    return width;
}

} // namespace fe::term

namespace fe {
/// The `` `citation` `` convention lives in fe::term, next to the renderer that reads it.
using term::Cite;        ///< @copydoc fe::term::Cite
using term::cite_string; ///< @copydoc fe::term::cite_string
using term::Cited;       ///< @copydoc fe::term::Cited
using term::format_cite; ///< @copydoc fe::term::format_cite
} // namespace fe

#ifndef DOXYGEN
template<class T>
struct std::formatter<fe::term::detail::Escaped<T>> {
    std::string_view spec; ///< Borrowed from the format string, which outlives the `vformat` call.

    constexpr auto parse(std::format_parse_context& ctx) {
        auto i = ctx.begin();
        for (; i != ctx.end() && *i != '}'; ++i)
            if (*i == '{') throw std::format_error("fe::term::format_cite: a nested replacement field needs Cite");
        spec = std::string_view(ctx.begin(), i);
        return i;
    }

    auto format(const fe::term::detail::Escaped<T>& escaped, std::format_context& ctx) const {
        auto str = spec.empty() ? std::format("{}", escaped.val)
                                : std::vformat(std::format("{{:{}}}", spec), std::make_format_args(escaped.val));
        return fe::term::escape_cite_to(ctx.out(), str);
    }
};

template<>
struct std::formatter<fe::term::Cite> : std::formatter<std::string_view> {
    auto format(fe::term::Cite cite, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(cite.view(), ctx);
    }
};

template<>
struct std::formatter<fe::term::FG> : fe::ostream_formatter {};
#endif
