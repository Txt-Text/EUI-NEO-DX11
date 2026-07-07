#include "core/window/window_backend.h"

#include "core/platform/native_bridge.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>


namespace core::window {

namespace {

constexpr const wchar_t* kWindowClassName = L"EUI_NEO_DX11_WINDOW";

std::vector<HWND>& windows() {
    static std::vector<HWND> handles;
    return handles;
}

std::unordered_map<HWND, HICON>& windowIcons() {
    static std::unordered_map<HWND, HICON> icons;
    return icons;
}


std::wstring utf8ToWide(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), size);
    return result;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

LONG roundLong(float value) {
    return static_cast<LONG>(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

bool isDescendantWindow(HWND root, HWND candidate) {
    HWND current = candidate;
    while (current != nullptr) {
        if (current == root) {
            return true;
        }
        current = GetParent(current);
    }
    return false;
}

bool windowOwnsPointer(HWND hwnd) {
    if (hwnd == nullptr) {
        return false;
    }
    const HWND capture = GetCapture();
    if (capture != nullptr) {
        return capture == hwnd || isDescendantWindow(hwnd, capture);
    }

    POINT point{};
    if (!GetCursorPos(&point)) {
        return false;
    }
    const HWND hovered = WindowFromPoint(point);
    return hovered == hwnd || isDescendantWindow(hwnd, hovered);
}

void registerWindowClass() {

    static bool registered = false;
    if (registered) {
        return;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.lpszClassName = kWindowClassName;
    RegisterClassExW(&windowClass);
    registered = true;
}

} // namespace

Handle createWindow(const WindowCreateRequest& request) {
    registerWindowClass();

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!request.resizable) {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    RECT rect{0, 0, std::max(1, request.width), std::max(1, request.height)};
    AdjustWindowRectEx(&rect, style, FALSE, 0);

    HWND parent = static_cast<HWND>(request.parent);
    const std::wstring title = utf8ToWide(request.title != nullptr ? request.title : "");
    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        title.empty() ? L"EUI-NEO" : title.c_str(),
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);
    if (hwnd != nullptr) {
        windows().push_back(hwnd);
    }
    return hwnd;
}

void destroyWindow(Handle window) {
    HWND hwnd = static_cast<HWND>(window);
    if (hwnd == nullptr) {
        return;
    }
    auto& handles = windows();
    handles.erase(std::remove(handles.begin(), handles.end(), hwnd), handles.end());
    auto& icons = windowIcons();
    const auto iconIt = icons.find(hwnd);
    if (iconIt != icons.end()) {
        DestroyIcon(iconIt->second);
        icons.erase(iconIt);
    }
    DestroyWindow(hwnd);
}


NativeWindowInfo nativeWindowInfo(Handle window) {
    NativeWindowInfo result;
    result.handle = window;
    result.platformWindow = window;
    return result;
}

ContextKey currentContextKey() {
    return nullptr;
}

double timeSeconds() {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER counter{};
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter) || frequency.QuadPart <= 0) {
        return 0.0;
    }
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

void postEmptyEvent() {
    for (HWND hwnd : windows()) {
        PostMessageW(hwnd, WM_NULL, 0, 0);
    }
}

void getCursorPosition(Handle window, double& x, double& y) {
    HWND hwnd = static_cast<HWND>(window);
    if (!windowOwnsPointer(hwnd)) {
        x = -1000000.0;
        y = -1000000.0;
        return;
    }

    POINT point{};
    GetCursorPos(&point);
    ScreenToClient(hwnd, &point);
    x = static_cast<double>(point.x);
    y = static_cast<double>(point.y);
}

bool isMouseButtonDown(Handle window, int button) {
    if (!windowOwnsPointer(static_cast<HWND>(window))) {
        return false;
    }
    const int key = button == 1 ? VK_RBUTTON : VK_LBUTTON;
    return (GetKeyState(key) & 0x8000) != 0;
}


std::string clipboardText(Handle window) {
    if (!OpenClipboard(static_cast<HWND>(window))) {
        return {};
    }
    std::string result;
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle != nullptr) {
        const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
        if (text != nullptr) {
            result = wideToUtf8(text);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

void setClipboardText(const std::string& text) {
    const std::wstring wide = utf8ToWide(text.c_str());
    if (!OpenClipboard(nullptr)) {
        return;
    }
    EmptyClipboard();
    const SIZE_T bytes = (wide.size() + 1u) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
        void* data = GlobalLock(memory);
        if (data != nullptr) {
            std::memcpy(data, wide.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory != nullptr) {
        GlobalFree(memory);
    }
    CloseClipboard();
}

CursorHandle createStandardCursor(CursorType type) {
    return LoadCursorW(nullptr, type == CursorType::Hand ? IDC_HAND : IDC_ARROW);
}

void setCursor(Handle, CursorHandle cursor) {
    SetCursor(static_cast<HCURSOR>(cursor));
}

void destroyCursor(CursorHandle) {}

void setWindowIcon(Handle window, int width, int height, unsigned char* pixels) {
    HWND hwnd = static_cast<HWND>(window);
    if (hwnd == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }

    BITMAPV5HEADER bitmapHeader{};
    bitmapHeader.bV5Size = sizeof(bitmapHeader);
    bitmapHeader.bV5Width = width;
    bitmapHeader.bV5Height = -height;
    bitmapHeader.bV5Planes = 1;
    bitmapHeader.bV5BitCount = 32;
    bitmapHeader.bV5Compression = BI_BITFIELDS;
    bitmapHeader.bV5RedMask = 0x00FF0000;
    bitmapHeader.bV5GreenMask = 0x0000FF00;
    bitmapHeader.bV5BlueMask = 0x000000FF;
    bitmapHeader.bV5AlphaMask = 0xFF000000;

    void* dibPixels = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP colorBitmap = CreateDIBSection(screen,
                                           reinterpret_cast<BITMAPINFO*>(&bitmapHeader),
                                           DIB_RGB_COLORS,
                                           &dibPixels,
                                           nullptr,
                                           0);
    ReleaseDC(nullptr, screen);
    if (colorBitmap == nullptr || dibPixels == nullptr) {
        if (colorBitmap != nullptr) {
            DeleteObject(colorBitmap);
        }
        return;
    }

    auto* destination = static_cast<unsigned char*>(dibPixels);
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const unsigned char r = pixels[i * 4 + 0];
        const unsigned char g = pixels[i * 4 + 1];
        const unsigned char b = pixels[i * 4 + 2];
        const unsigned char a = pixels[i * 4 + 3];
        destination[i * 4 + 0] = b;
        destination[i * 4 + 1] = g;
        destination[i * 4 + 2] = r;
        destination[i * 4 + 3] = a;
    }

    HBITMAP maskBitmap = CreateBitmap(width, height, 1, 1, nullptr);
    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = maskBitmap;
    iconInfo.hbmColor = colorBitmap;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(maskBitmap);
    DeleteObject(colorBitmap);
    if (icon == nullptr) {
        return;
    }

    auto& icons = windowIcons();
    const auto existing = icons.find(hwnd);
    if (existing != icons.end()) {
        DestroyIcon(existing->second);
    }
    icons[hwnd] = icon;

    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    eui_set_application_icon_rgba(width, height, pixels);
}


void setImeCursorRect(Handle window, float x, float y, float width, float height) {
    HWND hwnd = static_cast<HWND>(window);
    if (hwnd == nullptr) {
        return;
    }
    HIMC context = ImmGetContext(hwnd);
    if (context == nullptr) {
        return;
    }

    const LONG caretX = roundLong(x);
    const LONG caretY = roundLong(y + height);
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos.x = caretX;
    composition.ptCurrentPos.y = caretY;
    composition.rcArea.left = roundLong(x);
    composition.rcArea.top = roundLong(y);
    composition.rcArea.right = roundLong(x + width);
    composition.rcArea.bottom = roundLong(y + height);
    ImmSetCompositionWindow(context, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos.x = caretX;
    candidate.ptCurrentPos.y = roundLong(y + height * 0.45f);
    candidate.rcArea = composition.rcArea;
    ImmSetCandidateWindow(context, &candidate);

    LOGFONTW font{};
    font.lfHeight = -roundLong(std::max(12.0f, height));
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(font.lfFaceName, LF_FACESIZE, L"Microsoft YaHei UI");
    ImmSetCompositionFontW(context, &font);
    ImmReleaseContext(hwnd, context);
}

} // namespace core::window
