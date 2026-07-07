#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

int eui_win32_entry();

int main() {
    return eui_win32_entry();
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return eui_win32_entry();
}
