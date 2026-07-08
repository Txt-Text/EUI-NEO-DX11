#pragma once

#include "core/platform/platform.h"

namespace eui::platform {

using core::platform::FileDialogOptions;
using core::platform::FileDialogResult;
using core::platform::FileDialogStatus;
using core::platform::TrayOptions;
using core::platform::WindowSystemMenuCommand;
using core::platform::beginWindowDrag;
using core::platform::chooseFile;
using core::platform::chooseFiles;
using core::platform::closeWindow;
using core::platform::isWindowMaximized;
using core::platform::isWindowTopmost;
using core::platform::maximizeWindow;
using core::platform::minimizeWindow;
using core::platform::openFileDialog;
using core::platform::openUrl;
using core::platform::restoreWindow;
using core::platform::setWindowTopmost;
using core::platform::showWindowSystemMenu;

} // namespace eui::platform
