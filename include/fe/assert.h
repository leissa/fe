#pragma once

#include <cassert>

#include <utility>

#ifdef _MSC_VER
#    include <intrin.h>
#endif

namespace fe {

[[noreturn]] inline void unreachable() {
    assert(false);
    std::unreachable();
}

/// Raise a breakpoint in the debugger.
inline void breakpoint() {
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__clang__)
    __builtin_debugtrap();
#elif defined(__x86_64__) || defined(__i386__)
    asm("int3");
#else
    __builtin_trap();
#endif
}

} // namespace fe

#ifndef NDEBUG
#    define assert_unused(x) assert(x)
#else
#    define assert_unused(x) ((void)(0 && (x)))
#endif
