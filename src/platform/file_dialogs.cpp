#include "platform/file_dialogs.hpp"

// nfd_glfw3.h needs GLFW's native handles to parent the dialog to our window.
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <nfd_glfw3.h>

namespace vig {

namespace fs = std::filesystem;

namespace {

// The first filter is selected by default; "All files" is appended by NFD.
constexpr nfdu8filteritem_t kFilters[] = {
    {"VIG sources", "vigas,c,h"},
    {"VIG assembly", "vigas"},
    {"C sources", "c,h"},
};
constexpr nfdfiltersize_t kFilterCount = sizeof(kFilters) / sizeof(kFilters[0]);

std::string path_string(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

fs::path from_utf8(const char* s) {
    const std::string str(s);
    return fs::path(std::u8string(str.begin(), str.end()));
}

}  // namespace

NativeFileDialogs::NativeFileDialogs(GLFWwindow* parent) : parent_(parent) {
    if (NFD_Init() != NFD_OKAY) {
        const char* err = NFD_GetError();
        error_ = err ? err : "NFD_Init failed";
        return;
    }
    initialized_ = true;
    NFD_SetDisplayPropertiesFromGLFW();  // Wayland: share our wl_display
}

NativeFileDialogs::~NativeFileDialogs() {
    if (initialized_) NFD_Quit();
}

std::optional<fs::path> NativeFileDialogs::open_file(const fs::path& dir) {
    if (!initialized_) return std::nullopt;
    error_.clear();

    const std::string default_path = path_string(dir);
    nfdopendialogu8args_t args{};
    args.filterList = kFilters;
    args.filterCount = kFilterCount;
    args.defaultPath = default_path.empty() ? nullptr : default_path.c_str();
    NFD_GetNativeWindowFromGLFWWindow(parent_, &args.parentWindow);

    nfdu8char_t* out = nullptr;
    const nfdresult_t result = NFD_OpenDialogU8_With(&out, &args);
    if (result == NFD_OKAY) {
        fs::path path = from_utf8(out);
        NFD_FreePathU8(out);
        return path;
    }
    if (result == NFD_ERROR) {
        const char* err = NFD_GetError();
        error_ = err ? err : "file dialog failed";
    }
    return std::nullopt;
}

std::optional<fs::path> NativeFileDialogs::save_file(const fs::path& dir, const std::string& name) {
    if (!initialized_) return std::nullopt;
    error_.clear();

    const std::string default_path = path_string(dir);
    nfdsavedialogu8args_t args{};
    args.filterList = kFilters;
    args.filterCount = kFilterCount;
    args.defaultPath = default_path.empty() ? nullptr : default_path.c_str();
    args.defaultName = name.empty() ? nullptr : name.c_str();
    NFD_GetNativeWindowFromGLFWWindow(parent_, &args.parentWindow);

    nfdu8char_t* out = nullptr;
    const nfdresult_t result = NFD_SaveDialogU8_With(&out, &args);
    if (result == NFD_OKAY) {
        fs::path path = from_utf8(out);
        NFD_FreePathU8(out);
        return path;
    }
    if (result == NFD_ERROR) {
        const char* err = NFD_GetError();
        error_ = err ? err : "file dialog failed";
    }
    return std::nullopt;
}

}  // namespace vig
