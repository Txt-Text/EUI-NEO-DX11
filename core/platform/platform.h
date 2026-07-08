#pragma once

#include <string>
#include <vector>

#include "core/window/window_backend.h"

namespace core::platform {

struct TrayOptions {
    std::string tooltip;
    std::string iconPath;
};

struct FileDialogOptions {
    std::string prompt;
    std::vector<std::string> allowedExtensions;
    std::string initialDirectory;
    std::string filterName;
    bool allowMultiple = false;
};

enum class FileDialogStatus {
    Selected,
    Cancelled,
    Failed
};

enum class WindowSystemMenuCommand {
    None = 0,
    Restore,
    Move,
    Size,
    Minimize,
    Maximize,
    Close
};

struct FileDialogResult {
    FileDialogStatus status = FileDialogStatus::Cancelled;
    std::vector<std::string> paths;
    std::string error;

    bool selected() const {
        return status == FileDialogStatus::Selected && !paths.empty();
    }
};

bool repairCurrentWorkingDirectory();
bool openUrl(const std::string& url);
FileDialogResult openFileDialog(const FileDialogOptions& options = {});
std::string chooseFile(const FileDialogOptions& options = {});
std::vector<std::string> chooseFiles(const FileDialogOptions& options = {});
bool initializeTray(const TrayOptions& options);
bool isTrayInitialized();
void pollTray(bool blocking = false);
bool consumeTrayShowRequested();
bool consumeTrayExitRequested();
void shutdownTray();
void setImeCursorRect(window::Handle window, float x, float y, float width, float height);
void beginWindowDrag(window::Handle window);
void closeWindow(window::Handle window);
void minimizeWindow(window::Handle window);
void maximizeWindow(window::Handle window);
void restoreWindow(window::Handle window);
bool isWindowMaximized(window::Handle window);
void setWindowTopmost(window::Handle window, bool topmost);
bool isWindowTopmost(window::Handle window);
WindowSystemMenuCommand showWindowSystemMenu(window::Handle window, int screenX, int screenY);
std::string windowIconDataUri(window::Handle window);
void requestFrame();

void requestUiUpdate();
bool consumeUiUpdate();
bool consumeFrameRequest();

} // namespace core::platform
