#include "fe/term.h"

#include <cstdlib>
#include <cstring>

#include <atomic>
#include <iterator>
#include <ostream>

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

namespace fe::term {

namespace {

bool env_set(const char* name) noexcept {
    auto* value = std::getenv(name);
    return value && *value != '\0';
}

bool env_is(const char* name, const char* expected) noexcept {
    auto* value = std::getenv(name);
    return value && std::strcmp(value, expected) == 0;
}

Mode default_mode() noexcept {
    if (env_set("NO_COLOR")) return Mode::Never;
    if (env_set("CLICOLOR_FORCE") && !env_is("CLICOLOR_FORCE", "0")) return Mode::Always;
    if (env_is("CLICOLOR", "0")) return Mode::Never;
    return Mode::Auto;
}

std::atomic<Mode> current_mode(default_mode());
std::atomic<bool> current_auto_detached(false);

#ifdef _WIN32
bool enable_vt(HANDLE handle) noexcept {
    if (handle == INVALID_HANDLE_VALUE) return false;

    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) return false;
    if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) return true;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
#endif

} // namespace

Mode mode() noexcept { return current_mode.load(std::memory_order_relaxed); }
void set_mode(Mode m) noexcept { current_mode.store(m, std::memory_order_relaxed); }

bool auto_detached() noexcept { return current_auto_detached.load(std::memory_order_relaxed); }
void set_auto_detached(bool b) noexcept { current_auto_detached.store(b, std::memory_order_relaxed); }

namespace detail {

Stream stream(std::ostream& os) noexcept {
    static std::streambuf* stdout_rdbuf = std::cout.rdbuf();
    static std::streambuf* stderr_rdbuf = std::cerr.rdbuf();
    static std::streambuf* clog_rdbuf   = std::clog.rdbuf();

    auto* const buf = os.rdbuf();
    if (buf == stdout_rdbuf) return Stream::Stdout;
    if (buf == stderr_rdbuf || buf == clog_rdbuf) return Stream::Stderr;
    return Stream::Unknown;
}

#ifdef _WIN32
bool is_terminal(Stream s) noexcept {
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
bool is_terminal(Stream s) noexcept {
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

size_t tick(std::string_view str, size_t i) noexcept {
    for (; i != str.size(); ++i)
        if (escape(str, i, str.size()))
            ++i;
        else if (str[i] == '`')
            return i;
    return std::string_view::npos;
}

void stream_raw(std::ostream& os, std::string_view str, size_t begin, size_t end) {
    for (auto i = begin; i != end; ++i) {
        if (escape(str, i, end)) ++i;
        os << str[i];
    }
}

size_t raw_width(std::string_view str, size_t begin, size_t end) noexcept {
    size_t width = 0;
    for (auto i = begin; i != end; ++i, ++width)
        if (escape(str, i, end)) ++i;
    return width;
}

} // namespace detail

bool use_color(std::ostream& os) noexcept {
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

std::optional<size_t> width(std::ostream& os) noexcept {
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

void resolve_mode(std::ostream& os) noexcept { set_auto_detached(detail::is_terminal(detail::stream(os))); }

std::ostream& operator<<(std::ostream& os, FG color) {
    if (use_color(os)) {
        auto esc = detail::sgr(color);
        os.write(esc.data(), esc.size());
    }
    return os;
}

std::string escape_cite(std::string_view str) {
    auto res = std::string();
    res.reserve(str.size());
    escape_cite_to(std::back_inserter(res), str);
    return res;
}

void render_cite(std::ostream& os, std::string_view str, bool color) {
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

void render_cite(std::ostream& os, std::string_view str) { render_cite(os, str, use_color(os)); }

size_t cite_width(std::string_view str, bool color) {
    size_t width = 0;
    detail::scan_cite(str, [&](size_t begin, size_t end, bool cited) {
        width += detail::raw_width(str, begin, end) + (cited && !color ? 2 : 0); // the backticks stay
    });
    return width;
}

} // namespace fe::term
