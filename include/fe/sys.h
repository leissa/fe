#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fe::sys {

/// Name of the command that locates an executable on this platform.
static constexpr auto which =
#ifdef _WIN32
    "where";
#else
    "which";
#endif

/// Path of the executable or dynamic library that contains @p addr.
/// Pass the address of a function of your own to find out where *you* have been loaded from.
/// @returns `std::nullopt` if an error occurred or the platform is not supported.
std::optional<std::filesystem::path> path_to_lib(const void* addr);

/// Executes command @p cmd.
/// @returns the output as string.
std::string exec(std::string cmd);

/// Locates @p cmd via sys::which; the resulting path may not exist.
std::string find_cmd(std::string cmd);

/// Thrown by sys::require_cmd when a command cannot be located on the system.
class CmdNotFound : public std::logic_error {
public:
    CmdNotFound(const std::string& s)
        : std::logic_error(s) {}
};

/// Locates @p name on the system or throws CmdNotFound.
std::string require_cmd(std::string_view name);

/// Wraps `std::system` and makes the return value usable.
int system(std::string cmd);

/// Runs @p cmd via sys::system and throws if it exits with a non-zero status.
void require_run(const std::string& cmd);

/// Wraps sys::system and puts `.exe` at the back (Windows) and `./` at the front (otherwise) of @p cmd.
int run(std::string cmd, std::string args = {});

/// Returns the @p path as `std::string` and escapes all whitespaces with backslash.
std::string escape(const std::filesystem::path& path);

} // namespace fe::sys
