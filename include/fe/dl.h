#pragma once

namespace fe::dl {

/// File name extension of a dynamic library on this platform.
static constexpr auto Ext =
#if defined(_WIN32)
    "dll";
#else
    "so";
#endif

/// Loads the dynamic library @p filename or throws.
void* open(const char* filename);

/// Looks up @p symbol_name in @p handle or throws.
void* get(void* handle, const char* symbol_name);

/// Unloads @p handle or throws.
void close(void* handle);

} // namespace fe::dl
