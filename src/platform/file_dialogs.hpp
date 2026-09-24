#pragma once

#include <filesystem>
#include <optional>
#include <string>

struct GLFWwindow;

namespace vig {

// Native Open / Save As dialogs (nativefiledialog-extended: Win32, Cocoa,
// GTK or xdg-desktop-portal). Calls block until the dialog closes, so make
// them between frames.
class NativeFileDialogs {
public:
    explicit NativeFileDialogs(GLFWwindow* parent);
    ~NativeFileDialogs();

    NativeFileDialogs(const NativeFileDialogs&) = delete;
    NativeFileDialogs& operator=(const NativeFileDialogs&) = delete;

    // nullopt when the user cancels or on failure (then error() is set).
    std::optional<std::filesystem::path> open_file(const std::filesystem::path& dir);
    // The native dialog asks before overwriting an existing file.
    std::optional<std::filesystem::path> save_file(const std::filesystem::path& dir, const std::string& name);

    const std::string& error() const { return error_; }

private:
    GLFWwindow* parent_;
    bool initialized_ = false;
    std::string error_;
};

}  // namespace vig
