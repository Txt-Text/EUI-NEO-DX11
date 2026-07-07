#include "core/render/dx11/dx11_backend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite_3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxgi.lib")

namespace core::render {

namespace {

struct FontSelection {
    std::vector<std::wstring> families;
    std::string fontFilePath;
};

D2D1_POINT_2F point(const core::Vec2& value) {
    return D2D1::Point2F(value.x, value.y);
}

D2D1_POINT_2F point(const core::render::PrimitiveGeometryVertex& value) {
    return D2D1::Point2F(value.screen.x, value.screen.y);
}

std::wstring utf8ToWide(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return {};
    }
    const int sourceLength = static_cast<int>(std::strlen(text));
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, sourceLength, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, sourceLength, result.data(), size);
    return result;
}

std::filesystem::path executableDirectory() {
    namespace fs = std::filesystem;
    char executablePath[MAX_PATH] = {};
    const DWORD size = GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return {};
    }
    std::error_code error;
    return fs::absolute(fs::path(executablePath), error).parent_path();
}

std::string firstExistingPath(std::initializer_list<std::filesystem::path> candidates) {
    namespace fs = std::filesystem;
    std::error_code error;
    for (const fs::path& candidate : candidates) {
        if (candidate.empty()) {
            continue;
        }
        error.clear();
        if (fs::exists(candidate, error) && !error) {
            return fs::absolute(candidate, error).string();
        }
    }
    return {};
}

std::string resolveFontFilePath(const std::string& path) {
    namespace fs = std::filesystem;
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const fs::path requested(path);
    const fs::path current = fs::current_path(error);
    const fs::path exeDir = executableDirectory();
    return firstExistingPath({
        requested,
        current / requested,
        current / "assets" / requested.filename(),
        exeDir / requested,
        exeDir / "assets" / requested.filename()
    });
}

void ensurePrivateFontRegistered(const std::string& path) {
    static std::unordered_set<std::string> registeredFonts;
    if (path.empty() || registeredFonts.find(path) != registeredFonts.end()) {
        return;
    }
    if (AddFontResourceExA(path.c_str(), FR_PRIVATE, nullptr) > 0) {
        registeredFonts.insert(path);
    }
}

bool isIconFontFamily(const std::string& family) {
    return family == "FontAwesome" || family == "Font Awesome" ||
           family == "Font Awesome 7 Free" || family == "Font Awesome 7 Free Solid" ||
           family == "Icon";
}

FontSelection candidateFontSelection(const char* family) {
    if (family == nullptr || family[0] == '\0') {
        ensurePrivateFontRegistered(resolveFontFilePath("JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf"));
        return {{L"JingNanJunJunTi", L"Segoe UI"}, {}};
    }

    const std::string name(family);
    if (name == "monospace") {
        return {{L"Consolas", L"Cascadia Mono", L"Segoe UI"}, {}};
    }
    if (name == "Emoji") {
        return {{L"Segoe UI Emoji", L"Segoe UI Symbol", L"Segoe UI"}, {}};
    }
    if (name == "YouSheBiaoTiHei" || name == "YouShe") {
        ensurePrivateFontRegistered(resolveFontFilePath("YouSheBiaoTiHei-2.ttf"));
        return {{L"YouSheBiaoTiHei", L"Segoe UI"}, {}};
    }
    if (name == "JingNanJunJunTi" || name == "JingNan") {
        ensurePrivateFontRegistered(resolveFontFilePath("JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf"));
        return {{L"JingNanJunJunTi", L"Segoe UI"}, {}};
    }
        if (isIconFontFamily(name)) {
        const std::string path = resolveFontFilePath("Font Awesome 7 Free-Solid-900.otf");
        ensurePrivateFontRegistered(path);
        return {{L"Font Awesome 7 Free", L"Segoe UI Symbol"}, path};
    }

    return {{utf8ToWide(family), L"Segoe UI"}, {}};
}

Microsoft::WRL::ComPtr<IDWriteFontCollection> cachedFontCollectionFromFile(IDWriteFactory* factory,
                                                                            const std::string& fontFilePath) {
    static std::map<std::string, Microsoft::WRL::ComPtr<IDWriteFontCollection>> cache;
    if (factory == nullptr || fontFilePath.empty()) {
        return {};
    }
    const auto cached = cache.find(fontFilePath);
    if (cached != cache.end()) {
        return cached->second;
    }

    Microsoft::WRL::ComPtr<IDWriteFactory5> factory5;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory5))) || factory5 == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> fontSetBuilder;
    if (FAILED(factory5->CreateFontSetBuilder(&fontSetBuilder)) || fontSetBuilder == nullptr) {
        return {};
    }

    const std::wstring widePath = utf8ToWide(fontFilePath.c_str());
    FILETIME lastWriteTime{};
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (GetFileAttributesExW(widePath.c_str(), GetFileExInfoStandard, &attributes)) {
        lastWriteTime = attributes.ftLastWriteTime;
    }

    Microsoft::WRL::ComPtr<IDWriteFontFaceReference> faceReference;
    if (FAILED(factory5->CreateFontFaceReference(widePath.c_str(), &lastWriteTime, 0, DWRITE_FONT_SIMULATIONS_NONE, &faceReference)) ||
        faceReference == nullptr) {
        return {};
    }
    if (FAILED(fontSetBuilder->AddFontFaceReference(faceReference.Get()))) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontSet> fontSet;
    if (FAILED(fontSetBuilder->CreateFontSet(&fontSet)) || fontSet == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontCollection1> collection1;
    if (FAILED(factory5->CreateFontCollectionFromFontSet(fontSet.Get(), &collection1)) || collection1 == nullptr) {
        return {};
    }

    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
    collection1.As(&collection);
    cache.emplace(fontFilePath, collection);
    return collection;
}

DWRITE_FONT_WEIGHT effectiveFontWeight(const char* family, int fontWeight) {
        const std::string name = family != nullptr ? std::string(family) : std::string();
    const int weight = isIconFontFamily(name) ? std::max(fontWeight, 900) : fontWeight;
    return static_cast<DWRITE_FONT_WEIGHT>(std::clamp(weight, 1, 999));
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

} // namespace

struct Dx11Backend::TextureResource {
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    int width = 0;
    int height = 0;
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
        createWindowSizeDependentResources(surface.framebufferWidth, surface.framebufferHeight);
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
    if (usingExternalContext_) {
        restoreFrameDrawingState();
    }
    if (d2dContext_ != nullptr && !hostManagesBeginEndDraw_) {
        d2dContext_->EndDraw();
    }
    if (swapChain_ != nullptr && !hostManagesPresent_) {
        swapChain_->Present(1, 0);
    }
}

bool Dx11Backend::ensureRenderCache(int width, int height) {
    (void)width;
    (void)height;
    return false;
}

bool Dx11Backend::renderCacheWasRecreated() const {
    return renderCacheRecreated_;
}

void Dx11Backend::releaseRenderCache() {
    releaseWindowSizeDependentResources();
}

void Dx11Backend::beginRenderCacheFrame(int width, int height, const std::vector<core::Rect>& repaintRects) {
    (void)width;
    (void)height;
    (void)repaintRects;
}

void Dx11Backend::endRenderCacheFrame() {}

void Dx11Backend::blitRenderCache(int width, int height, RenderCacheBlitMode mode, const std::vector<core::Rect>& dirtyRects) {
    (void)width;
    (void)height;
    (void)mode;
    (void)dirtyRects;
}

void Dx11Backend::clear(const core::Color& color) {
    if (usingExternalContext_) {
        if (d2dContext_ != nullptr) {
            d2dContext_->Clear(toD2DColor(color));
        }
        return;
    }
    if (renderTargetView_ != nullptr && d3dContext_ != nullptr) {
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

    const float radius = std::clamp(command.radius, 0.0f, std::min(command.rect.width, command.rect.height) * 0.5f);
    const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
        D2D1::RectF(command.rect.x,
                    command.rect.y,
                    command.rect.x + command.rect.width,
                    command.rect.y + command.rect.height),
        radius,
        radius);

    if (command.gradient.enabled && !command.shadowPass) {
        const D2D1_POINT_2F start = command.gradient.direction == core::GradientDirection::Horizontal
            ? D2D1::Point2F(command.rect.x, command.rect.y)
            : D2D1::Point2F(command.rect.x, command.rect.y);
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
    if (d2dContext_ == nullptr || dwriteFactory_ == nullptr || command.utf8Text == nullptr || command.utf8Text[0] == '\0') {
        return;
    }

        const std::wstring text = utf8ToWide(command.utf8Text);
    const FontSelection selection = candidateFontSelection(command.fontFamily);
    const std::vector<std::wstring>& families = selection.families;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> customCollection = cachedFontCollectionFromFile(dwriteFactory_.Get(), selection.fontFilePath);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
            for (const std::wstring& family : families) {
        if (customCollection != nullptr &&
            SUCCEEDED(dwriteFactory_->CreateTextFormat(family.c_str(),
                                                       customCollection.Get(),
                                                       effectiveFontWeight(command.fontFamily, command.fontWeight),
                                                       DWRITE_FONT_STYLE_NORMAL,
                                                       DWRITE_FONT_STRETCH_NORMAL,
                                                       std::max(1.0f, command.fontSize),
                                                       L"",
                                                       &format))) {
            break;
        }
        if (SUCCEEDED(dwriteFactory_->CreateTextFormat(family.c_str(),
                                                       nullptr,
                                                       effectiveFontWeight(command.fontFamily, command.fontWeight),
                                                       DWRITE_FONT_STYLE_NORMAL,
                                                       DWRITE_FONT_STRETCH_NORMAL,
                                                       std::max(1.0f, command.fontSize),
                                                       L"",
                                                       &format))) {
            break;
        }
    }

                    if (format == nullptr) {
        return;
    }

    format->SetTextAlignment(command.horizontalAlign == HorizontalAlign::Center
                                 ? DWRITE_TEXT_ALIGNMENT_CENTER
                                 : command.horizontalAlign == HorizontalAlign::Right
                                       ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                       : DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(command.verticalAlign == VerticalAlign::Center
                                      ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                      : command.verticalAlign == VerticalAlign::Bottom
                                            ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                            : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(command.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);

            const float layoutWidth = command.maxWidth > 0.0f ? command.maxWidth : 4096.0f;
    const float layoutHeight = command.maxHeight > 0.0f ? std::max(command.maxHeight, command.fontSize * 1.5f) : 4096.0f;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwriteFactory_->CreateTextLayout(text.c_str(),
                                                static_cast<UINT32>(text.size()),
                                                format.Get(),
                                                layoutWidth,
                                                layoutHeight,
                                                &layout))) {
        return;
    }

    if (command.lineHeight > 0.0f) {
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                               command.lineHeight,
                               command.fontSize * 0.8f);
    }

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
    d2dContext_->DrawTextLayout(D2D1::Point2F(command.originX, command.originY), layout.Get(), brush.Get());
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
    if (texture->bitmap == nullptr) {
        return nullptr;
    }
    return texture.release();
}

bool Dx11Backend::updateTexture(TextureHandle handle, const unsigned char* pixels, int width, int height) {
    auto* texture = static_cast<TextureResource*>(handle);
    if (texture == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
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

void Dx11Backend::drawTexture(TextureHandle handle,
                              const float* vertices,
                              std::size_t vertexFloatCount,
                              const core::Color& tint,
                              const core::Rect& rect,
                              float radius,
                              int windowWidth,
                              int windowHeight) {
    (void)vertexFloatCount;
    (void)rect;
    (void)radius;
    (void)windowWidth;
    (void)windowHeight;

    auto* texture = static_cast<TextureResource*>(handle);
    if (texture == nullptr || texture->bitmap == nullptr || vertices == nullptr || d2dContext_ == nullptr) {
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1BitmapBrush1> brush;
    if (FAILED(d2dContext_->CreateBitmapBrush(texture->bitmap.Get(), &brush)) || brush == nullptr) {
        return;
    }
    brush->SetExtendModeX(D2D1_EXTEND_MODE_CLAMP);
    brush->SetExtendModeY(D2D1_EXTEND_MODE_CLAMP);
    brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    brush->SetOpacity(std::clamp(tint.a, 0.0f, 1.0f));
    brush->SetTransform(textureTransformFromVertices(vertices));

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
    if (FAILED(d2dFactory_->CreatePathGeometry(&geometry)) || geometry == nullptr) {
        return;
    }
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geometry->Open(&sink)) || sink == nullptr) {
        return;
    }

    sink->BeginFigure(D2D1::Point2F(vertices[0], vertices[1]), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(D2D1::Point2F(vertices[7], vertices[8]));
    sink->AddLine(D2D1::Point2F(vertices[14], vertices[15]));
    sink->AddLine(D2D1::Point2F(vertices[35], vertices[36]));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) {
        return;
    }

    d2dContext_->FillGeometry(geometry.Get(), brush.Get());
}

bool Dx11Backend::createDeviceResources() {
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
    swapDesc.Width = 0;
    swapDesc.Height = 0;
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
    renderCacheRecreated_ = true;
    return true;
}

void Dx11Backend::releaseWindowSizeDependentResources() {
    if (clipActive_ && d2dContext_ != nullptr) {
        d2dContext_->PopAxisAlignedClip();
        clipActive_ = false;
    }
    frameDrawingStateSaved_ = false;
    if (usingExternalContext_) {
        drawingStateBlock_.Reset();
        renderTargetView_.Reset();
        d2dTargetBitmap_.Reset();
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