#include "fe/dl.h"

#include "fe/assert.h"

#ifdef _WIN32
#    include <windows.h>
#else
#    include <dlfcn.h>
#endif

namespace fe::dl {

void* open(const char* file) {
#ifdef _WIN32
    if (HMODULE handle = LoadLibraryA(file)) {
        return static_cast<void*>(handle);
    } else {
        throwf("could not load dynamic library `{}` due to error `{}`\n"
               "see https://docs.microsoft.com/en-us/windows/win32/debug/system-error-codes",
               file, GetLastError());
    }
#else
    if (void* handle = dlopen(file, RTLD_NOW))
        return handle;
    else if (auto err = dlerror())
        throwf("could not load dynamic library `{}` due to error `{}`", file, err);
    else
        throwf("could not load dynamic library `{}`", file);
#endif
}

void* get(void* handle, const char* symbol) {
#ifdef _WIN32
    if (auto addr = GetProcAddress(static_cast<HMODULE>(handle), symbol)) {
        return reinterpret_cast<void*>(addr);
    } else {
        throwf("could not find symbol `{}` due to error `{}`\n"
               "see https://docs.microsoft.com/en-us/windows/win32/debug/system-error-codes",
               symbol, GetLastError());
    }
#else
    dlerror(); // clear error state
    void* addr = dlsym(handle, symbol);
    if (auto err = dlerror())
        throwf("could not find symbol `{}` due to error `{}`", symbol, err);
    else
        return addr;
#endif
}

void close(void* handle) {
#ifdef _WIN32
    if (!FreeLibrary(static_cast<HMODULE>(handle))) throwf("`FreeLibrary()` failed");
#else
    if (auto err = dlclose(handle)) throwf("`dlclose()` failed with error code `{}`", err);
#endif
}

} // namespace fe::dl
