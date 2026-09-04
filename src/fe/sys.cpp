#include "fe/sys.h"

#include <cstdio>

#include <array>
#include <iostream>
#include <memory>

#include "fe/assert.h"
#include "fe/utf8.h"

#ifdef _WIN32
#    include <windows.h>
#    define popen  _popen
#    define pclose _pclose
#    define WEXITSTATUS
#elif defined(__APPLE__) || defined(__linux__)
#    include <dlfcn.h>
#endif

using namespace std::string_literals;

namespace fs = std::filesystem;

namespace fe::sys {

std::optional<fs::path> path_to_lib([[maybe_unused]] const void* addr) {
#if defined(_WIN32)
    HMODULE mod = nullptr;
    auto flags  = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(addr), &mod)) return {};

    std::wstring buf;
    buf.resize(512);
    while (true) {
        DWORD len = GetModuleFileNameW(mod, buf.data(), (DWORD)buf.size());
        if (len == 0) return {};

        if (len < buf.size() - 1) {
            buf.resize(len);
            break;
        }

        buf.resize(buf.size() * 2); // buffer too small
    }

    return fs::weakly_canonical(fs::path(buf));
#elif defined(__APPLE__) || defined(__linux__)
    Dl_info info;
    if (dladdr(addr, &info) == 0) return {};
    return fs::weakly_canonical(info.dli_fname);
#else
    return {};
#endif
}

// see https://stackoverflow.com/a/478960
std::string exec(std::string cmd) {
    using PipeCloser = int (*)(FILE*); // spell out type explicitly to get rid of warning
    if (auto pipe = std::unique_ptr<FILE, PipeCloser>(popen(cmd.c_str(), "r"), pclose)) {
        std::array<char, 128> buffer;
        std::string result;
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
            result += buffer.data();
        return result;
    } else
        throwf("`popen()` failed");
}

std::string find_cmd(std::string cmd) {
    auto out = exec(Which + " "s + cmd);
    if (auto it = out.find('\n'); it != std::string::npos) out.erase(it);
    return out;
}

std::string require_cmd(std::string_view name) {
    auto cmd = find_cmd(std::string(name));
    if (!fs::exists(cmd))
        throwf<CmdNotFound>("could not find command `{}`; resolved path `{}` does not exist", name, cmd);
    return cmd;
}

int system(std::string cmd) {
    std::cout << cmd << std::endl;
    int status = std::system(cmd.c_str());
    return WEXITSTATUS(status);
}

void require_run(const std::string& cmd) {
    if (auto rc = sys::system(cmd); rc != 0) throwf("command `{}` exited with error code {}", cmd, rc);
}

int run(std::string cmd, std::string args /* = {}*/) {
#ifdef _WIN32
    cmd += ".exe";
#else
    cmd = "./"s + cmd;
#endif
    return sys::system(cmd + " "s + args);
}

std::string escape(const fs::path& path) {
    std::string str;
    for (char c : path.string()) {
        if (utf8::isspace(static_cast<unsigned char>(c))) str += '\\';
        str += c;
    }
    return str;
}

} // namespace fe::sys
