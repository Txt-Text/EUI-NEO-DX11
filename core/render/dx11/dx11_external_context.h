#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>

namespace core::render {

struct ExternalDx11Context {
    HWND hwnd = nullptr;

    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    IDXGIFactory2* dxgiFactory = nullptr;

    ID2D1Factory1* d2dFactory = nullptr;
    ID2D1Device* d2dDevice = nullptr;
    ID2D1DeviceContext* d2dContext = nullptr;
    IDWriteFactory* dwriteFactory = nullptr;

    IDXGISwapChain1* swapChain = nullptr;
    ID3D11RenderTargetView* d3dRenderTargetView = nullptr;
    ID2D1Bitmap1* d2dTargetBitmap = nullptr;

    bool hostManagesPresent = true;
    bool hostManagesResize = true;
    bool hostManagesBeginEndDraw = true;
};

void setExternalDx11Context(const ExternalDx11Context& context);
bool hasExternalDx11Context();
bool hasExternalDx11Context(HWND hwnd);
const ExternalDx11Context* externalDx11Context();
const ExternalDx11Context* externalDx11Context(HWND hwnd);
void clearExternalDx11Context();
void clearExternalDx11Context(HWND hwnd);

} // namespace core::render
