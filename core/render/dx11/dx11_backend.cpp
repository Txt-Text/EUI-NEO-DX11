#include "core/render/dx11/dx11_backend.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite_3.h>
#include "core/render/dx11/dx11_font_support.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")
namespace core::render {
namespace {
D2D1_POINT_2F point(const core::Vec2& value) {
    return D2D1::Point2F(value.x, value.y);
}
D2D1_POINT_2F point(const core::render::PrimitiveGeometryVertex& value) {
    return D2D1::Point2F(value.screen.x, value.screen.y);
}
D2D1_MATRIX_3X2_F textureTransformFromVertices(const float* vertices) {
    const float x0 = vertices[3];
    const float y0 = vertices[4];
    const float x1 = vertices[10];
    const float y1 = vertices[11];
    const float x3 = vertices[38];
    const float y3 = vertices[39];
    const float sx0 = vertices[0];
    const float sy0 = vertices[1];
    const float sx1 = vertices[7];
    const float sy1 = vertices[8];
    const float sx3 = vertices[35];
    const float sy3 = vertices[36];
    const float width = std::max(0.0001f, x1 - x0);
    const float height = std::max(0.0001f, y3 - y0);
    return D2D1::Matrix3x2F((sx1 - sx0) / width,
                            (sy1 - sy0) / width,
                            (sx3 - sx0) / height,
                            (sy3 - sy0) / height,
                            sx0 - x0 * ((sx1 - sx0) / width) - y0 * ((sx3 - sx0) / height),
                            sy0 - x0 * ((sy1 - sy0) / width) - y0 * ((sy3 - sy0) / height));
}
D2D1_MATRIX_3X2_F primitiveTransformFromVertices(const std::vector<PrimitiveGeometryVertex>& vertices) {
    if (vertices.size() < 6) {
        return D2D1::Matrix3x2F::Identity();
    }
    const float x0 = vertices[0].local.x;
    const float y0 = vertices[0].local.y;
    const float x1 = vertices[1].local.x;
    const float y1 = vertices[1].local.y;
    const float x3 = vertices[5].local.x;
    const float y3 = vertices[5].local.y;
    const float sx0 = vertices[0].screen.x;
    const float sy0 = vertices[0].screen.y;
    const float sx1 = vertices[1].screen.x;
    const float sy1 = vertices[1].screen.y;
    const float sx3 = vertices[5].screen.x;
    const float sy3 = vertices[5].screen.y;
    const float width = std::max(0.0001f, x1 - x0);
    const float height = std::max(0.0001f, y3 - y0);
    return D2D1::Matrix3x2F((sx1 - sx0) / width,
                            (sy1 - sy0) / width,
                            (sx3 - sx0) / height,
                            (sy3 - sy0) / height,
                            sx0 - x0 * ((sx1 - sx0) / width) - y0 * ((sx3 - sx0) / height),
                            sy0 - x0 * ((sy1 - sy0) / width) - y0 * ((sy3 - sy0) / height));
}
} // namespace
struct Dx11Backend::TextureResource {
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    int width = 0;
    int height = 0;
    bool layer = false;
};
Dx11Backend::Dx11Backend(core::window::Handle window)
    : hwnd_(static_cast<HWND>(window)) {}
Dx11Backend::~Dx11Backend() {
    releaseRenderCache();
}
bool Dx11Backend::initialize() {
    if (hwnd_ == nullptr) {
        return false;
    }
    if (const ExternalDx11Context* context = externalDx11Context(hwnd_)) {
        valid_ = initializeExternalResources(*context);
    } else {
        valid_ = initializeOwnedResources();
    }
    return valid_;
}
bool Dx11Backend::initializeOwnedResources() {
    usingExternalContext_ = false;
    frameDrawingStateSaved_ = false;
    hostManagesPresent_ = false;
    hostManagesResize_ = false;
    hostManagesBeginEndDraw_ = false;
    renderCacheFrameActive_ = false;
    layerFrameActive_ = false;
    drawingStateBlock_.Reset();
    return createDeviceResources();
}
bool Dx11Backend::initializeExternalResources(const ExternalDx11Context& context) {
    if (context.hwnd != hwnd_) {
        return false;
    }
    if (!applyExternalContext(context, true)) {
        return false;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    currentWidth_ = std::max(1L, client.right - client.left);
    currentHeight_ = std::max(1L, client.bottom - client.top);
    renderCacheRecreated_ = true;
    return true;
}
bool Dx11Backend::refreshExternalResources() {
    const ExternalDx11Context* context = externalDx11Context(hwnd_);
    return context != nullptr && applyExternalContext(*context, false);
}
bool Dx11Backend::applyExternalContext(const ExternalDx11Context& context, bool initializeState) {
    if (context.hwnd != hwnd_ ||
        context.d3dDevice == nullptr ||
        context.d3dContext == nullptr ||
        context.d2dContext == nullptr ||
        context.dwriteFactory == nullptr) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1Factory1> resolvedFactory;
    if (context.d2dFactory != nullptr) {
        resolvedFactory = context.d2dFactory;
    } else {
        Microsoft::WRL::ComPtr<ID2D1Factory> baseFactory;
        context.d2dContext->GetFactory(&baseFactory);
        if (baseFactory != nullptr) {
            baseFactory.As(&resolvedFactory);
        }
    }
    if (resolvedFactory == nullptr) {
        return false;
    }
    const bool deviceChanged = d3dDevice_.Get() != context.d3dDevice;
    const bool factoryChanged = d2dFactory_.Get() != resolvedFactory.Get();
    usingExternalContext_ = true;
    hostManagesPresent_ = context.hostManagesPresent;
    hostManagesResize_ = context.hostManagesResize;
    hostManagesBeginEndDraw_ = context.hostManagesBeginEndDraw;
    d3dDevice_ = context.d3dDevice;
    d3dContext_ = context.d3dContext;
    d2dFactory_ = resolvedFactory;
    d2dDevice_ = context.d2dDevice;
    d2dContext_ = context.d2dContext;
    dwriteFactory_ = context.dwriteFactory;
    swapChain_ = context.swapChain;
    renderTargetView_ = context.d3dRenderTargetView;
    d2dTargetBitmap_ = context.d2dTargetBitmap;
    if (deviceChanged || initializeState) {
        rasterizerState_.Reset();
    }
    if (factoryChanged || initializeState) {
        drawingStateBlock_.Reset();
    }
    if (rasterizerState_ == nullptr) {
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.ScissorEnable = TRUE;
        rasterizer.DepthClipEnable = TRUE;
        if (FAILED(d3dDevice_->CreateRasterizerState(&rasterizer, &rasterizerState_))) {
            return false;
        }
    }
    if (drawingStateBlock_ == nullptr) {
        Microsoft::WRL::ComPtr<ID2D1DrawingStateBlock> drawingStateBlock;
        if (FAILED(d2dFactory_->CreateDrawingStateBlock(&drawingStateBlock)) || drawingStateBlock == nullptr) {
            return false;
        }
        if (FAILED(drawingStateBlock.As(&drawingStateBlock_))) {
            return false;
        }
    }
    return true;
}
void Dx11Backend::saveFrameDrawingState() {
    if (frameDrawingStateSaved_ || d2dContext_ == nullptr || drawingStateBlock_ == nullptr) {
        return;
    }
    d2dContext_->SaveDrawingState(drawingStateBlock_.Get());
    frameDrawingStateSaved_ = true;
}
void Dx11Backend::restoreFrameDrawingState() {
    if (!frameDrawingStateSaved_ || d2dContext_ == nullptr || drawingStateBlock_ == nullptr) {
        return;
    }
    if (clipActive_) {
        d2dContext_->PopAxisAlignedClip();
        clipActive_ = false;
    }
    d2dContext_->RestoreDrawingState(drawingStateBlock_.Get());
    frameDrawingStateSaved_ = false;
}
bool Dx11Backend::valid() const {
    return valid_;
}
void Dx11Backend::makeCurrent() {}
void Dx11Backend::beginFrame(const RenderSurface& surface) {
    if (!valid_) {
        return;
    }
    renderCacheRecreated_ = false;
    const bool sizeChanged = surface.framebufferWidth != currentWidth_ || surface.framebufferHeight != currentHeight_;
    if (usingExternalContext_) {
        if (!refreshExternalResources()) {
            valid_ = false;
            return;
        }
        if (sizeChanged) {
            currentWidth_ = std::max(1, surface.framebufferWidth);
            currentHeight_ = std::max(1, surface.framebufferHeight);
            renderCacheRecreated_ = true;
        }
        if (d2dContext_ != nullptr) {
            saveFrameDrawingState();
            if (d2dTargetBitmap_ != nullptr) {
                d2dContext_->SetTarget(d2dTargetBitmap_.Get());
            }
            if (!hostManagesBeginEndDraw_) {
                d2dContext_->BeginDraw();
            }
            d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
            d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        }
        return;
    }
    if (sizeChanged || renderTargetView_ == nullptr || d2dTargetBitmap_ == nullptr) {
        if (!createWindowSizeDependentResources(surface.framebufferWidth, surface.framebufferHeight)) {
            valid_ = false;
            return;
        }
    }
    if (d2dContext_ != nullptr && d2dTargetBitmap_ != nullptr) {
        d2dContext_->SetTarget(d2dTargetBitmap_.Get());
        d2dContext_->BeginDraw();
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        d2dContext_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    }
}
void Dx11Backend::present() {
    if (d2dContext_ != nullptr && !hostManagesBeginEndDraw_) {
        d2dContext_->EndDraw();
    }
    if (usingExternalContext_) {
        restoreFrameDrawingState();
    }
    if (swapChain_ != nullptr && !hostManagesPresent_) {
        swapChain_->Present(1, 0);
    }
}
bool Dx11Backend::ensureRenderCache(int width, int height) {
    if (d2dContext_ == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (usingExternalContext_) {
        return false;
    }
    if (renderCacheBitmap_ != nullptr && currentWidth_ == width && currentHeight_ == height) {
        return true;
    }
    // render cache 始终是一张离屏 target bitmap，尺寸与当前窗口 framebuffer 对齐。
    renderCacheBitmap_ = createTargetBitmap(width, height, D2D1_BITMAP_OPTIONS_TARGET);

    renderCacheRecreated_ = renderCacheBitmap_ != nullptr;
    return renderCacheBitmap_ != nullptr;
}

bool Dx11Backend::renderCacheWasRecreated() const {
    return renderCacheRecreated_;
}
void Dx11Backend::releaseRenderCache() {
    // 这里只释放缓存相关 target，不销毁整套 device；窗口级资源由 releaseWindowSizeDependentResources() 处理。
    renderCacheBitmap_.Reset();
    frameTargetImage_.Reset();
    layerRestoreTargetImage_.Reset();
    renderCacheFrameActive_ = false;
    layerFrameActive_ = false;

    if (usingExternalContext_) {
        if (clipActive_ && d2dContext_ != nullptr) {
            d2dContext_->PopAxisAlignedClip();
            clipActive_ = false;
        }
        if (d2dContext_ != nullptr) {
            d2dContext_->SetTarget(nullptr);
        }
        frameDrawingStateSaved_ = false;
        renderTargetView_.Reset();
        d2dTargetBitmap_.Reset();
        backBufferTexture_.Reset();
        swapChain_.Reset();
        renderCacheRecreated_ = true;
        return;
    }
    releaseWindowSizeDependentResources();
}
void Dx11Backend::beginRenderCacheFrame(int width, int height, const std::vector<core::Rect>& repaintRects) {
    (void)repaintRects;
    if (!ensureRenderCache(width, height) || d2dContext_ == nullptr || renderCacheBitmap_ == nullptr) {
        return;
    }
    if (d2dTargetBitmap_ != nullptr) {
        d2dTargetBitmap_.As(&frameTargetImage_);
    }
    // 进入 render cache 帧时，把 D2D 输出目标从窗口 backbuffer 切到离屏 bitmap。
    setActiveTarget(renderCacheBitmap_.Get());
    renderCacheFrameActive_ = true;
}

void Dx11Backend::endRenderCacheFrame() {
    if (!renderCacheFrameActive_ || d2dContext_ == nullptr) {
        return;
    }
    if (d2dTargetBitmap_ != nullptr) {
        // render cache 绘制结束后，目标切回窗口 target，后续 blit 才会落到屏幕。
        setActiveTarget(d2dTargetBitmap_.Get());
    }
    renderCacheFrameActive_ = false;
}

void Dx11Backend::blitRenderCache(int width, int height, RenderCacheBlitMode mode, const std::vector<core::Rect>& dirtyRects) {
    if (d2dContext_ == nullptr || renderCacheBitmap_ == nullptr || width <= 0 || height <= 0) {
        return;
    }
    if (mode == RenderCacheBlitMode::Dirty && !dirtyRects.empty()) {
        // dirty 模式只把脏矩形从离屏缓存拷回窗口 target。
        for (const core::Rect& dirty : dirtyRects) {
            const D2D1_RECT_F rect = D2D1::RectF(dirty.x, dirty.y, dirty.x + dirty.width, dirty.y + dirty.height);
            d2dContext_->DrawBitmap(renderCacheBitmap_.Get(), rect, 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, rect);
        }
        return;
    }

    const D2D1_RECT_F fullRect = D2D1::RectF(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    d2dContext_->DrawBitmap(renderCacheBitmap_.Get(), fullRect, 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, fullRect);
}
void Dx11Backend::clear(const core::Color& color) {
    if (usingExternalContext_) {
        if (d2dContext_ != nullptr) {
            d2dContext_->Clear(toD2DColor(color));
        }
        return;
    }
    if (renderTargetView_ != nullptr && d3dContext_ != nullptr && !renderCacheFrameActive_ && !layerFrameActive_) {
        // 只有当前直接画窗口 backbuffer 时，才走 D3D clear；离屏 target 统一走 D2D clear。
        const float rgba[4] = {color.r, color.g, color.b, color.a};

        d3dContext_->OMSetRenderTargets(1, renderTargetView_.GetAddressOf(), nullptr);
        d3dContext_->ClearRenderTargetView(renderTargetView_.Get(), rgba);
        return;
    }
    if (d2dContext_ != nullptr) {
        d2dContext_->Clear(toD2DColor(color));
    }
}
void Dx11Backend::setScissor(bool enabled, const core::Rect& rect, int framebufferHeight) {
    (void)framebufferHeight;
    activeClipRect_ = rect;
    if (d3dContext_ == nullptr || rasterizerState_ == nullptr) {
        return;
    }
    d3dContext_->RSSetState(rasterizerState_.Get());
    if (clipActive_ && d2dContext_ != nullptr) {
        d2dContext_->PopAxisAlignedClip();
        clipActive_ = false;
    }
    if (enabled) {
        D3D11_RECT scissor{};
        scissor.left = static_cast<LONG>(std::floor(rect.x));
        scissor.top = static_cast<LONG>(std::floor(rect.y));
        scissor.right = static_cast<LONG>(std::ceil(rect.x + rect.width));
        scissor.bottom = static_cast<LONG>(std::ceil(rect.y + rect.height));
        d3dContext_->RSSetScissorRects(1, &scissor);
        if (d2dContext_ != nullptr) {
            d2dContext_->PushAxisAlignedClip(D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height),
                                             D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            clipActive_ = true;
        }
    }
}
void Dx11Backend::prepareBackdropBlur(const core::Rect& bounds, float blur, int windowWidth, int windowHeight) {
    (void)bounds;
    (void)blur;
    (void)windowWidth;
    (void)windowHeight;
}
void Dx11Backend::drawRoundedRect(const RoundedRectDrawCommand& command, int windowWidth, int windowHeight) {
    (void)windowWidth;
    (void)windowHeight;
    if (d2dContext_ == nullptr || command.rect.width <= 0.0f || command.rect.height <= 0.0f) {
        return;
    }
    D2D1_MATRIX_3X2_F previousTransform;
    d2dContext_->GetTransform(&previousTransform);
    d2dContext_->SetTransform(primitiveTransformFromVertices(command.vertices) * previousTransform);
    const float radius = std::clamp(command.radius, 0.0f, std::min(command.rect.width, command.rect.height) * 0.5f);
    const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
        D2D1::RectF(command.rect.x,
                    command.rect.y,
                    command.rect.x + command.rect.width,
                    command.rect.y + command.rect.height),
        radius,
        radius);
    if (command.gradient.enabled && !command.shadowPass) {
        const D2D1_POINT_2F start = D2D1::Point2F(command.rect.x, command.rect.y);
        const D2D1_POINT_2F end = command.gradient.direction == core::GradientDirection::Horizontal
            ? D2D1::Point2F(command.rect.x + command.rect.width, command.rect.y)
            : D2D1::Point2F(command.rect.x, command.rect.y + command.rect.height);
        D2D1_GRADIENT_STOP stops[2] = {
            {0.0f, toD2DColor(command.gradient.start, command.opacity)},
            {1.0f, toD2DColor(command.gradient.end, command.opacity)}
        };
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stopCollection;
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> brush;
        if (SUCCEEDED(d2dContext_->CreateGradientStopCollection(stops, 2, &stopCollection)) &&
            SUCCEEDED(d2dContext_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(start, end),
                                                             stopCollection.Get(),
                                                             &brush))) {
            d2dContext_->FillRoundedRectangle(roundedRect, brush.Get());
        }
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(d2dContext_->CreateSolidColorBrush(toD2DColor(command.fillColor, command.opacity), &brush))) {
            d2dContext_->FillRoundedRectangle(roundedRect, brush.Get());
        }
    }
    if (!command.shadowPass && command.border.width > 0.0f && command.border.color.a > 0.001f) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(d2dContext_->CreateSolidColorBrush(toD2DColor(command.border.color, command.opacity), &brush))) {
            d2dContext_->DrawRoundedRectangle(roundedRect, brush.Get(), command.border.width);
        }
    }
    d2dContext_->SetTransform(previousTransform);
}
void Dx11Backend::drawPolygon(const PolygonDrawCommand& command, int windowWidth, int windowHeight) {
    (void)windowWidth;
    (void)windowHeight;
    if (d2dContext_ == nullptr || command.vertices.size() < 3) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(d2dFactory_->CreatePathGeometry(&geometry))) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(&sink))) {
        return;
    }
    sink->BeginFigure(point(command.vertices[0]), D2D1_FIGURE_BEGIN_FILLED);
    for (std::size_t i = 1; i < command.vertices.size(); ++i) {
        sink->AddLine(point(command.vertices[i]));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    fillGeometryWithColor(geometry.Get(), command.fillColor, command.opacity);
}
void Dx11Backend::drawText(const TextDrawCommand& command, int windowWidth, int windowHeight) {
    (void)windowWidth;
    (void)windowHeight;
    if (d2dContext_ == nullptr || command.preparedLayout == nullptr ||
        command.utf8Text == nullptr || command.utf8Text[0] == '\0') {
        return;
    }
    auto* layout = static_cast<IDWriteTextLayout*>(command.preparedLayout);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(d2dContext_->CreateSolidColorBrush(toD2DColor(command.color), &brush))) {
        return;
    }
    D2D1_MATRIX_3X2_F previousTransform;
    d2dContext_->GetTransform(&previousTransform);
    if (command.hasTransformMatrix) {
        d2dContext_->SetTransform(D2D1::Matrix3x2F(command.transformMatrix.m00,
                                                   command.transformMatrix.m10,
                                                   command.transformMatrix.m01,
                                                   command.transformMatrix.m11,
                                                   command.transformMatrix.tx,
                                                   command.transformMatrix.ty));
    }
    d2dContext_->DrawTextLayout(D2D1::Point2F(command.originX, command.originY), layout, brush.Get());
    if (command.hasTransformMatrix) {
        d2dContext_->SetTransform(previousTransform);
    }
}
Dx11Backend::TextureHandle Dx11Backend::createTexture(const unsigned char* pixels, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }
    auto texture = std::make_unique<TextureResource>();
    texture->bitmap = createBitmapFromRgba(pixels, width, height);
    texture->width = width;
    texture->height = height;
    texture->layer = false;
    if (texture->bitmap == nullptr) {
        return nullptr;
    }
    return texture.release();
}
bool Dx11Backend::updateTexture(TextureHandle handle, const unsigned char* pixels, int width, int height) {
    auto* texture = static_cast<TextureResource*>(handle);
    if (texture == nullptr || texture->layer || pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap = createBitmapFromRgba(pixels, width, height);
    if (bitmap == nullptr) {
        return false;
    }
    texture->bitmap = bitmap;
    texture->width = width;
    texture->height = height;
    return true;
}
void Dx11Backend::destroyTexture(TextureHandle handle) {
    delete static_cast<TextureResource*>(handle);
}
Dx11Backend::LayerHandle Dx11Backend::createLayer(int width, int height) {
    if (d2dContext_ == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }
    // layer 本质上也是一张可作为 D2D target 的 bitmap，只是生命周期由 retained layer 管理。
    auto layer = std::make_unique<TextureResource>();
    layer->bitmap = createTargetBitmap(width, height, D2D1_BITMAP_OPTIONS_TARGET);

    layer->width = width;

    layer->height = height;
    layer->layer = true;
    if (layer->bitmap == nullptr) {
        return nullptr;
    }
    return layer.release();
}
bool Dx11Backend::resizeLayer(LayerHandle handle, int width, int height) {
    auto* layer = static_cast<TextureResource*>(handle);
    if (layer == nullptr || !layer->layer || width <= 0 || height <= 0) {
        return false;
    }
    if (layer->width == width && layer->height == height && layer->bitmap != nullptr) {
        return true;
        }
    layer->bitmap = createTargetBitmap(width, height, D2D1_BITMAP_OPTIONS_TARGET);

    if (layer->bitmap == nullptr) {
        layer->width = 0;
        layer->height = 0;
        return false;
    }
    layer->width = width;
    layer->height = height;
    return true;
}
void Dx11Backend::destroyLayer(LayerHandle handle) {
    delete static_cast<TextureResource*>(handle);
}
bool Dx11Backend::beginLayerFrame(LayerHandle handle, int width, int height) {
    auto* layer = static_cast<TextureResource*>(handle);
    if (layer == nullptr || !layer->layer || layer->bitmap == nullptr || d2dContext_ == nullptr) {
        return false;
    }
    if (layer->width != width || layer->height != height) {
        return false;
    }
    // 进入 retained layer 绘制前，先记住当前 target，结束时再恢复。
    d2dContext_->GetTarget(&layerRestoreTargetImage_);
    setActiveTarget(layer->bitmap.Get());
    layerFrameActive_ = true;
    return true;
}

void Dx11Backend::endLayerFrame() {
    if (!layerFrameActive_ || d2dContext_ == nullptr) {
        return;
    }
    d2dContext_->SetTarget(layerRestoreTargetImage_.Get());
    layerRestoreTargetImage_.Reset();
    layerFrameActive_ = false;
}
Dx11Backend::TextureHandle Dx11Backend::layerTexture(LayerHandle handle) {
    auto* layer = static_cast<TextureResource*>(handle);
    if (layer == nullptr || !layer->layer || layer->bitmap == nullptr) {
        return nullptr;
    }
    return handle;
}
void Dx11Backend::drawTexture(TextureHandle handle,
                              const float* vertices,
                              std::size_t vertexFloatCount,
                              const core::Color& tint,
                              const core::Rect& rect,
                              float radius,
                              int windowWidth,
                              int windowHeight) {
    (void)vertexFloatCount;
    (void)windowWidth;
    (void)windowHeight;
    auto* texture = static_cast<TextureResource*>(handle);
    if (texture == nullptr || texture->bitmap == nullptr || vertices == nullptr || d2dContext_ == nullptr) {
        return;
    }
    D2D1_MATRIX_3X2_F previousTransform;
    d2dContext_->GetTransform(&previousTransform);
    d2dContext_->SetTransform(textureTransformFromVertices(vertices) * previousTransform);
    const D2D1_RECT_F destRect = D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
    const float u0 = vertices[5];
    const float v0 = vertices[6];
    const float u1 = vertices[19];
    const float v1 = vertices[20];
    const D2D1_RECT_F sourceRect = D2D1::RectF(
        std::clamp(u0, 0.0f, 1.0f) * texture->width,
        std::clamp(v0, 0.0f, 1.0f) * texture->height,
        std::clamp(u1, 0.0f, 1.0f) * texture->width,
        std::clamp(v1, 0.0f, 1.0f) * texture->height);
    const float opacity = std::clamp(tint.a, 0.0f, 1.0f);
    if (radius > 0.0f) {
        Microsoft::WRL::ComPtr<ID2D1BitmapBrush1> brush;
        if (SUCCEEDED(d2dContext_->CreateBitmapBrush(texture->bitmap.Get(), &brush)) && brush != nullptr) {
            brush->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);
            brush->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);
            brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            brush->SetOpacity(opacity);
            brush->SetTransform(D2D1::Matrix3x2F::Translation(rect.x - sourceRect.left, rect.y - sourceRect.top));
            const float clampedRadius = std::clamp(radius, 0.0f, std::min(rect.width, rect.height) * 0.5f);
            const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(destRect, clampedRadius, clampedRadius);
            d2dContext_->FillRoundedRectangle(roundedRect, brush.Get());
        }
        d2dContext_->SetTransform(previousTransform);
        return;
    }
    d2dContext_->DrawBitmap(texture->bitmap.Get(),
                            destRect,
                            opacity,
                            D2D1_INTERPOLATION_MODE_LINEAR,
                            sourceRect);
    d2dContext_->SetTransform(previousTransform);
}
bool Dx11Backend::createDeviceResources() {
    dx11::warmDefaultFontCaches();
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr,
                                   D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr,
                                   flags,
                                   featureLevels,
                                   ARRAYSIZE(featureLevels),
                                   D3D11_SDK_VERSION,
                                   &d3dDevice_,
                                   &featureLevel,
                                   &d3dContext_);
    if (FAILED(hr)) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3dDevice_.As(&dxgiDevice))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (FAILED(factory->CreateSwapChainForHwnd(d3dDevice_.Get(), hwnd_, &swapDesc, nullptr, nullptr, &swapChain_))) {
        return false;
    }
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    D2D1_FACTORY_OPTIONS factoryOptions{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 __uuidof(ID2D1Factory1),
                                 &factoryOptions,
                                 reinterpret_cast<void**>(d2dFactory_.GetAddressOf())))) {
        return false;
    }
    if (FAILED(d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_))) {
        return false;
    }
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_))) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf())))) {
        return false;
    }
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D11_FILL_SOLID;
    rasterizer.CullMode = D3D11_CULL_NONE;
    rasterizer.ScissorEnable = TRUE;
    rasterizer.DepthClipEnable = TRUE;
    d3dDevice_->CreateRasterizerState(&rasterizer, &rasterizerState_);
    RECT client{};
    GetClientRect(hwnd_, &client);
    return createWindowSizeDependentResources(std::max(1L, client.right - client.left),
                                              std::max(1L, client.bottom - client.top));
}
bool Dx11Backend::createWindowSizeDependentResources(int width, int height) {
    if (usingExternalContext_) {
        if (!refreshExternalResources()) {
            return false;
        }
        currentWidth_ = std::max(1, width);
        currentHeight_ = std::max(1, height);
        renderCacheBitmap_.Reset();
        renderCacheRecreated_ = true;
        return d2dContext_ != nullptr;
    }
    if (swapChain_ == nullptr || d3dDevice_ == nullptr || d2dContext_ == nullptr) {
        return false;
    }
    releaseWindowSizeDependentResources();
    currentWidth_ = std::max(1, width);
    currentHeight_ = std::max(1, height);
    if (FAILED(swapChain_->ResizeBuffers(0,
                                         static_cast<UINT>(currentWidth_),
                                         static_cast<UINT>(currentHeight_),
                                         DXGI_FORMAT_UNKNOWN,
                                         0))) {
        return false;
    }
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBufferTexture_)))) {
        return false;
    }
    if (FAILED(d3dDevice_->CreateRenderTargetView(backBufferTexture_.Get(), nullptr, &renderTargetView_))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGISurface> dxgiSurface;
    if (FAILED(backBufferTexture_.As(&dxgiSurface))) {
        return false;
    }
    const D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);
    if (FAILED(d2dContext_->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &bitmapProperties, &d2dTargetBitmap_))) {
        return false;
    }
    d2dContext_->SetTarget(d2dTargetBitmap_.Get());
    renderCacheBitmap_.Reset();
    renderCacheRecreated_ = true;
    return true;
}
Microsoft::WRL::ComPtr<ID2D1Bitmap1> Dx11Backend::createTargetBitmap(int width,
                                                                      int height,
                                                                      D2D1_BITMAP_OPTIONS options) const {
    if (d2dContext_ == nullptr || width <= 0 || height <= 0) {
        return {};
    }
    // 统一从这里创建可作为 SetTarget(...) 目标的离屏 bitmap。
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(

        options,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    if (FAILED(d2dContext_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
                                         nullptr,
                                         0,
                                         &properties,
                                         &bitmap))) {
        return {};
    }
    return bitmap;
}
void Dx11Backend::setActiveTarget(ID2D1Bitmap1* target) {
    if (d2dContext_ == nullptr) {
        return;
    }
    // 切 target 前先弹掉 clip，避免把上一个目标上的裁剪状态带到新目标里。
    if (clipActive_) {
        d2dContext_->PopAxisAlignedClip();
        clipActive_ = false;
    }
    d2dContext_->SetTarget(target);
}

void Dx11Backend::releaseWindowSizeDependentResources() {
    if (clipActive_ && d2dContext_ != nullptr) {
        d2dContext_->PopAxisAlignedClip();
        clipActive_ = false;
    }
    frameDrawingStateSaved_ = false;
    renderCacheFrameActive_ = false;
    layerFrameActive_ = false;
    // 这里清掉所有与当前窗口尺寸绑定的目标和恢复句柄，避免 resize 后复用旧 target。
    frameTargetImage_.Reset();
    layerRestoreTargetImage_.Reset();

    if (usingExternalContext_) {
        drawingStateBlock_.Reset();
        renderTargetView_.Reset();
        d2dTargetBitmap_.Reset();
        renderCacheBitmap_.Reset();
        backBufferTexture_.Reset();
        swapChain_.Reset();
        dwriteFactory_.Reset();
        d2dContext_.Reset();
        d2dDevice_.Reset();
        d2dFactory_.Reset();
        rasterizerState_.Reset();
        d3dContext_.Reset();
        d3dDevice_.Reset();
        usingExternalContext_ = false;
        hostManagesPresent_ = false;
        hostManagesResize_ = false;
        hostManagesBeginEndDraw_ = false;
        return;
    }
    if (d2dContext_ != nullptr) {
        d2dContext_->SetTarget(nullptr);
    }
    d2dTargetBitmap_.Reset();
    renderCacheBitmap_.Reset();
    renderTargetView_.Reset();
    backBufferTexture_.Reset();
}
D2D1_COLOR_F Dx11Backend::toD2DColor(const core::Color& color, float opacityScale) const {
    return D2D1::ColorF(color.r, color.g, color.b, color.a * opacityScale);
}
void Dx11Backend::fillGeometryWithColor(ID2D1Geometry* geometry, const core::Color& color, float opacity) {
    if (geometry == nullptr || d2dContext_ == nullptr) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(d2dContext_->CreateSolidColorBrush(toD2DColor(color, opacity), &brush))) {
        d2dContext_->FillGeometry(geometry, brush.Get());
    }
}
void Dx11Backend::strokeGeometryWithColor(ID2D1Geometry* geometry, const core::Color& color, float width, float opacity) {
    if (geometry == nullptr || d2dContext_ == nullptr || width <= 0.0f) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(d2dContext_->CreateSolidColorBrush(toD2DColor(color, opacity), &brush))) {
        d2dContext_->DrawGeometry(geometry, brush.Get(), width);
    }
}
Microsoft::WRL::ComPtr<ID2D1Bitmap1> Dx11Backend::createBitmapFromRgba(const unsigned char* pixels, int width, int height) const {
    if (d2dContext_ == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
        return {};
    }
    std::vector<unsigned char> bgra(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    for (std::size_t i = 0; i < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++i) {
        const unsigned char r = pixels[i * 4 + 0];
        const unsigned char g = pixels[i * 4 + 1];
        const unsigned char b = pixels[i * 4 + 2];
        const unsigned char a = pixels[i * 4 + 3];
        bgra[i * 4 + 0] = static_cast<unsigned char>((static_cast<unsigned int>(b) * a + 127u) / 255u);
        bgra[i * 4 + 1] = static_cast<unsigned char>((static_cast<unsigned int>(g) * a + 127u) / 255u);
        bgra[i * 4 + 2] = static_cast<unsigned char>((static_cast<unsigned int>(r) * a + 127u) / 255u);
        bgra[i * 4 + 3] = a;
    }
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f,
        96.0f);
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    if (FAILED(d2dContext_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
                                         bgra.data(),
                                         static_cast<UINT32>(width * 4),
                                         &properties,
                                         &bitmap))) {
        return {};
    }
    return bitmap;
}
} // namespace core::render