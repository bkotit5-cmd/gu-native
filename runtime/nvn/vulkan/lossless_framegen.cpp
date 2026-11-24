#include "lossless_framegen.h"

#ifdef __ANDROID__
#include <dlfcn.h>
#include <sys/stat.h>
#include <mutex>
#include <string>
#include <cstring>

namespace {
std::mutex g_lossless_mutex;
std::string g_lossless_dll_path;
void *g_bloodframe_handle;

bool regular_dll_file(const std::string &path) {
    if (path.empty()) return false;
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0x100 &&
           st.st_size <= (128LL << 20);
}

void *bloodframe_symbol(const char *name) {
    if (!g_bloodframe_handle) {
        // GameActivity.getLibraries() loads this private Android backend before libmain.so.
        g_bloodframe_handle = dlopen("libbloodframe.so", RTLD_NOW | RTLD_LOCAL);
    }
    return g_bloodframe_handle ? dlsym(g_bloodframe_handle, name) : nullptr;
}
} // namespace

extern "C" void lossless_framegen_set_dll_path(const char *path) {
    std::lock_guard<std::mutex> lock(g_lossless_mutex);
    g_lossless_dll_path = path ? path : "";
    if (!regular_dll_file(g_lossless_dll_path)) {
        g_lossless_dll_path.clear();
    }
}

extern "C" bool lossless_framegen_has_dll(void) {
    std::lock_guard<std::mutex> lock(g_lossless_mutex);
    return regular_dll_file(g_lossless_dll_path);
}

extern "C" bool lossless_framegen_library_available(void) {
    std::lock_guard<std::mutex> lock(g_lossless_mutex);
    static const char *const required[] = {
        "bb_lsfg_init", "bb_lsfg_create_context", "bb_lsfg_present", "bb_lsfg_wait",
        "bb_lsfg_destroy_context", "bb_lsfg_shutdown", "bb_lsfg_diagnostics"
    };
    for (const char *symbol : required) {
        if (!bloodframe_symbol(symbol)) return false;
    }
    return true;
}

extern "C" void lossless_framegen_get_dll_path(char *out, size_t capacity) {
    if (!out || capacity == 0) return;
    std::lock_guard<std::mutex> lock(g_lossless_mutex);
    const size_t n = g_lossless_dll_path.size() < capacity - 1 ? g_lossless_dll_path.size() : capacity - 1;
    std::memcpy(out, g_lossless_dll_path.data(), n);
    out[n] = '\0';
}
#else
extern "C" void lossless_framegen_set_dll_path(const char *) {}
extern "C" bool lossless_framegen_has_dll(void) { return false; }
extern "C" bool lossless_framegen_library_available(void) { return false; }
extern "C" void lossless_framegen_get_dll_path(char *out, size_t capacity) {
    if (out && capacity) out[0] = '\0';
}
#endif
