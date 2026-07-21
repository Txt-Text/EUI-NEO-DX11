#pragma once

#include "core/render/dx11/dx11_external_context.h"
#include "core/render/render_backend.h"

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
#include <wrl/client.h>

namespace core::render {

class Dx11Backend final : public RenderBackend {
public:
    explicit Dx11Backend(core::window::Handle window);
    ~Dx11Backend() override;

    bool initialize() override;
    bool valid() const override;
    void makeCurrent() override;
    void beginFrame(const RenderSurface& surface) override;
    void present() override;

    bool ensureRenderCache(int width, int height) override;
    bool renderCacheWasRecreated() const override;
    void releaseRenderCache() override;
    void beginRenderCacheFrame(int width, int height, const std::vector<core::Rect>& repaintRects) override;
    void endRenderCacheFrame() override;
    void blitRenderCache(int width, int height, RenderCacheBlitMode mode, const std::vector<core::Rect>& dirtyRects) override;

    void clear(const core::Color& color) override;
    void setScissor(bool enabled, const core::Rect& rect, int framebufferHeight) override;
    void prepareBackdropBlur(const core::Rect& bounds, float blur, int windowWidth, int windowHeight) override;
    void drawRoundedRect(const RoundedRectDrawCommand& command, int windowWidth, int windowHeight) override;
    void drawPolygon(const PolygonDrawCommand& command, int windowWidth, int windowHeight) override;
    void drawText(const TextDrawCommand& command, int windowWidth, int windowHeight) override;
    TextureHandle createTexture(const unsigned char* pixels, int width, int height) override;
    bool updateTexture(TextureHandle handle, const unsigned char* pixels, int width, int height) override;
    void destroyTexture(TextureHandle handle) override;
    LayerHandle createLayer(int width, int height) override;
    bool resizeLayer(LayerHandle layer, int width, int height) override;
    void destroyLayer(LayerHandle layer) override;
    bool beginLayerFrame(LayerHandle layer, int width, int height) override;
    void endLayerFrame() override;
    TextureHandle layerTexture(LayerHandle layer) override;
    void drawTexture(TextureHandle handle,
                     const float* vertices,
                     std::size_t vertexFloatCount,
                     const core::Color& tint,
                     const core::Rect& rect,
                     float radius,
                     int windowWidth,
                     int windowHeight) override;

private:
    struct TextureResource;

    bool initializeOwnedResources();
    bool initializeExternalResources(const ExternalDx11Context& context);
    bool refreshExternalResources();
    bool applyExternalContext(const ExternalDx11Context& context, bool initializeState);
    void saveFrameDrawingState();
    void restoreFrameDrawingState();
    bool createDeviceResources();
    bool createWindowSizeDependentResources(int width, int height);
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> createTargetBitmap(int width, int height, D2D1_BITMAP_OPTIONS options) const;
    void setActiveTarget(ID2D1Bitmap1* target);
    void releaseWindowSizeDependentResources();
    D2D1_COLOR_F toD2DColor(const core::Color& color, float opacityScale = 1.0f) const;
    void fillGeometryWithColor(ID2D1Geometry* geometry, const core::Color& color, float opacity);
    void strokeGeometryWithColor(ID2D1Geometry* geometry, const core::Color& color, float width, float opacity);
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> createBitmapFromRgba(const unsigned char* pixels, int width, int height) const;

    HWND hwnd_ = nullptr;
    int currentWidth_ = 0;
    int currentHeight_ = 0;
    bool valid_ = false;
    bool usingExternalContext_ = false;
    bool frameDrawingStateSaved_ = false;
    bool hostManagesPresent_ = false;
    bool hostManagesResize_ = false;
    bool hostManagesBeginEndDraw_ = false;
    bool renderCacheRecreated_ = false;
    bool clipActive_ = false;
    bool renderCacheFrameActive_ = false;
    bool layerFrameActive_ = false;
    core::Rect activeClipRect_{};

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBufferTexture_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<ID2D1DrawingStateBlock1> drawingStateBlock_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2dTargetBitmap_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> renderCacheBitmap_;
    Microsoft::WRL::ComPtr<ID2D1Image> frameTargetImage_;
    Microsoft::WRL::ComPtr<ID2D1Image> layerRestoreTargetImage_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
};



} // namespace core::render