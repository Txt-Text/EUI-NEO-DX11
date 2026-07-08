#pragma once

#include "core/window/window_types.h"

#include <string>

namespace core::window {

Handle createWindow(const WindowCreateRequest& request);
void destroyWindow(Handle window);
NativeWindowInfo nativeWindowInfo(Handle window);

ContextKey currentContextKey();
double timeSeconds();
void postEmptyEvent();

void getCursorPosition(Handle window, double& x, double& y);
bool isMouseButtonDown(Handle window, int button);
std::string clipboardText(Handle window);
void setClipboardText(const std::string& text);

CursorHandle createStandardCursor(CursorType type);
void setCursor(Handle window, CursorHandle cursor);
void destroyCursor(CursorHandle cursor);

void setWindowIcon(Handle window, int width, int height, unsigned char* pixels);
void setWindowCustomTitleBar(Handle window, bool enabled);
bool systemUsesDarkMode();
void refreshWindowTheme(Handle window);
void setImeCursorRect(Handle window, float x, float y, float width, float height);



} // namespace core::window
