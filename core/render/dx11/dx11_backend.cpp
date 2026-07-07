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
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
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

std::string wideToUtf8(const std::wstring& text) {

    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

unsigned int firstUtf8Codepoint(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }
    const unsigned char lead = static_cast<unsigned char>(text[0]);
    if ((lead & 0x80u) == 0) {
        return lead;
    }
    if ((lead & 0xE0u) == 0xC0u && text[1] != '\0') {
        return ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(text[1]) & 0x3Fu);
    }
    if ((lead & 0xF0u) == 0xE0u && text[1] != '\0' && text[2] != '\0') {
        return ((lead & 0x0Fu) << 12) |
               ((static_cast<unsigned char>(text[1]) & 0x3Fu) << 6) |
               (static_cast<unsigned char>(text[2]) & 0x3Fu);
    }
    if ((lead & 0xF8u) == 0xF0u && text[1] != '\0' && text[2] != '\0' && text[3] != '\0') {
        return ((lead & 0x07u) << 18) |
               ((static_cast<unsigned char>(text[1]) & 0x3Fu) << 12) |
               ((static_cast<unsigned char>(text[2]) & 0x3Fu) << 6) |
               (static_cast<unsigned char>(text[3]) & 0x3Fu);
    }
    return lead;
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
    (void)path;
}



bool isIconFontFamily(const std::string& family) {
    return family == "FontAwesome" || family == "Font Awesome" ||
           family == "Font Awesome 7 Free" || family == "Font Awesome 7 Free Solid" ||
           family == "Icon";
}

bool isFamilyVisibleInSystemCollection(IDWriteFactory* factory, const std::wstring& family) {
    if (factory == nullptr || family.empty()) {
        return false;
    }
    Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
    if (FAILED(factory->GetSystemFontCollection(&collection, TRUE)) || collection == nullptr) {
        return false;
    }
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (FAILED(collection->FindFamilyName(family.c_str(), &index, &exists))) {
        return false;
    }
    return exists == TRUE;
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



} // namespace

Dx11Backend::Dx11Backend(core::window::Handle window)
    : hwnd_(static_cast<HWND>(window)) {}

Dx11Backend::~Dx11Backend() {
    releaseRenderCache();
}

bool Dx11Backend::initialize() {
    if (hwnd_ == nullptr) {
        return false;
    }
    valid_ = createDeviceResources();
    return valid_;
}

bool Dx11Backend::valid() const {
    return valid_;
}

void Dx11Backend::makeCurrent() {}

void Dx11Backend::beginFrame(const RenderSurface& surface) {
    if (!valid_) {
        return;
    }
    if (surface.framebufferWidth != currentWidth_ || surface.framebufferHeight != currentHeight_ ||
        renderTargetView_ == nullptr || d2dTargetBitmap_ == nullptr) {
        createWindowSizeDependentResources(surface.framebufferWidth, surface.framebufferHeight);
    }
    if (d2dContext_ != nullptr && d2dTargetBitmap_ != nullptr) {
        d2dContext_->SetTarget(d2dTargetBitmap_.Get());
        d2dContext_->BeginDraw();
        d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
}

void Dx11Backend::present() {
    if (d2dContext_ != nullptr) {
        d2dContext_->EndDraw();
    }
    if (swapChain_ != nullptr) {
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
    if (renderTargetView_ == nullptr || d3dContext_ == nullptr) {
        return;
    }
    const float rgba[4] = {color.r, color.g, color.b, color.a};
    d3dContext_->OMSetRenderTargets(1, renderTargetView_.GetAddressOf(), nullptr);
    d3dContext_->ClearRenderTargetView(renderTargetView_.Get(), rgba);
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
    std::wstring chosenFamily;



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
            chosenFamily = family;
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
            chosenFamily = family;
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
    const std::wstring layoutText = text;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;

    const HRESULT layoutHr = dwriteFactory_->CreateTextLayout(layoutText.c_str(),
                                                              static_cast<UINT32>(layoutText.size()),
                                                              format.Get(),
                                                              layoutWidth,
                                                              layoutHeight,
                                                              &layout);

    if (FAILED(layoutHr)) {
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

    //const D2D1_MATRIX_3X2_F previousTransform = d2dContext_->GetTransform();
    D2D1_MATRIX_3X2_F previousTransform;
    d2dContext_->GetTransform(&previousTransform); // 传入地址接收数据

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

} // namespace core::render