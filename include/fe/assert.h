#pragma once

#include <cassert>
#include <cstdlib>

#include <format>
#include <stdexcept>
#include <utility>

namespace fe {

/// Throws a `T` (a `std::logic_error` by default) whose message is `std::format(fmt, args...)`.
/// Use this for unrecoverable errors that should surface as a proper exception with a formatted message.
template<class T = std::logic_error, class... Args>
[[noreturn]] void throwf(std::format_string<Args...> fmt, Args&&... args) {
    throw T("error: " + std::format(fmt, std::forward<Args>(args)...));
}

/// @sa https://stackoverflow.com/a/65258501
#ifdef __GNUC__ // GCC 4.8+, Clang, Intel and other compilers compatible with GCC (-std=c++0x or above)
[[noreturn]] inline __attribute__((always_inline)) void unreachable() {
    assert(false);
    __builtin_unreachable();
}
#elif defined(_MSC_VER) // MSVC
[[noreturn]] __forceinline void unreachable() {
    assert(false);
    __assume(false);
}
#else                   // ???
[[noreturn]] inline void unreachable() {
    assert(false);
    std::abort();
}
#endif

/// Raise a breakpoint in the debugger.
#if (defined(__clang__) || defined(__GNUC__)) && (defined(__x86_64__) || defined(__i386__))
inline void breakpoint() { asm("int3"); }
#else
inline void breakpoint() {
    volatile int* p = nullptr;
    *p              = 42;
}
#endif

} // namespace fe

#ifndef NDEBUG
#    define assert_unused(x) assert(x)
#else
#    define assert_unused(x) ((void)(0 && (x)))
#endif
