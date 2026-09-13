#include "fe/term.h"

#include <cstdlib>
#include <cstring>

#include <atomic>

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

} // namespace

Mode mode() noexcept { return current_mode.load(std::memory_order_relaxed); }
void set_mode(Mode m) noexcept { current_mode.store(m, std::memory_order_relaxed); }

bool auto_detached() noexcept { return current_auto_detached.load(std::memory_order_relaxed); }
void set_auto_detached(bool b) noexcept { current_auto_detached.store(b, std::memory_order_relaxed); }

} // namespace fe::term
