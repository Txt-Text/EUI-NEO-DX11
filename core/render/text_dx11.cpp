#include "core/render/text.h"

#include "core/render/render_backend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#pragma comment(lib, "dwrite.lib")

namespace core {

namespace {

using Microsoft::WRL::ComPtr;

struct Utf8Boundary {
    int byteIndex = 0;
    int utf16Index = 0;
};

std::string& defaultUiFontFileOverride() {
    static std::string path;
    return path;
}

std::string& defaultIconFontFileOverride() {
    static std::string path;
    return path;
}

std::unordered_set<std::string>& registeredFonts() {
    static std::unordered_set<std::string> paths;
    return paths;
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

std::string resolveDefaultUiFontPath() {
    const std::string& override = defaultUiFontFileOverride();
    return resolveFontFilePath(override.empty() ? "JingNanJunJunTi-JinNanJunJunTi-Bold-2.ttf" : override);
}

std::string resolveDefaultIconFontPath() {
    const std::string& override = defaultIconFontFileOverride();
    return resolveFontFilePath(override.empty() ? "Font Awesome 7 Free-Solid-900.otf" : override);
}

void ensurePrivateFontRegistered(const std::string& path) {
    if (path.empty()) {
        return;
    }
    auto& fonts = registeredFonts();
    if (fonts.find(path) != fonts.end()) {
        return;
    }
    if (AddFontResourceExA(path.c_str(), FR_PRIVATE, nullptr) > 0) {
        fonts.insert(path);
    }
}

bool isIconFontFamily(const std::string& family) {
    return family == "FontAwesome" || family == "Font Awesome" ||
           family == "Font Awesome 7 Free" || family == "Font Awesome 7 Free Solid" ||
           family == "Icon";
}

ComPtr<IDWriteFactory>& sharedDWriteFactory() {
    static ComPtr<IDWriteFactory> factory;
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                            __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
    }
    return factory;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring resolvedFontFamily(const std::string& family) {
    if (family == "monospace") {
        return L"Consolas";
    }
    if (family == "Emoji") {
        return L"Segoe UI Emoji";
    }
    if (family == "YouSheBiaoTiHei" || family == "YouShe") {
        ensurePrivateFontRegistered(resolveFontFilePath("YouSheBiaoTiHei-2.ttf"));
        return L"YouSheBiaoTiHei";
    }
    if (family == "JingNanJunJunTi" || family == "JingNan") {
        ensurePrivateFontRegistered(resolveDefaultUiFontPath());
        return L"JingNanJunJunTi";
    }
    if (isIconFontFamily(family)) {
        ensurePrivateFontRegistered(resolveDefaultIconFontPath());
        return L"Font Awesome 7 Free Solid";
    }
    if (family.empty()) {
        ensurePrivateFontRegistered(resolveDefaultUiFontPath());
        return L"JingNanJunJunTi";
    }
    return utf8ToWide(family);
}

DWRITE_FONT_WEIGHT effectiveFontWeight(const std::string& family, int fontWeight) {
    const int weight = isIconFontFamily(family) ? std::max(fontWeight, 900) : fontWeight;
    return static_cast<DWRITE_FONT_WEIGHT>(std::clamp(weight, 1, 999));
}

std::vector<Utf8Boundary> utf8Boundaries(const std::string& text) {
    std::vector<Utf8Boundary> boundaries;
    boundaries.push_back({0, 0});

    int utf16Index = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        std::size_t sequenceLength = 1;
        unsigned int codepoint = lead;
        if ((lead & 0xE0u) == 0xC0u && index + 1 < text.size()) {
            sequenceLength = 2;
            codepoint = ((lead & 0x1Fu) << 6) |
                        (static_cast<unsigned char>(text[index + 1]) & 0x3Fu);
        } else if ((lead & 0xF0u) == 0xE0u && index + 2 < text.size()) {
            sequenceLength = 3;
            codepoint = ((lead & 0x0Fu) << 12) |
                        ((static_cast<unsigned char>(text[index + 1]) & 0x3Fu) << 6) |
                        (static_cast<unsigned char>(text[index + 2]) & 0x3Fu);
        } else if ((lead & 0xF8u) == 0xF0u && index + 3 < text.size()) {
            sequenceLength = 4;
            codepoint = ((lead & 0x07u) << 18) |
                        ((static_cast<unsigned char>(text[index + 1]) & 0x3Fu) << 12) |
                        ((static_cast<unsigned char>(text[index + 2]) & 0x3Fu) << 6) |
                        (static_cast<unsigned char>(text[index + 3]) & 0x3Fu);
        }

        index += sequenceLength;
        utf16Index += codepoint > 0xFFFFu ? 2 : 1;
        boundaries.push_back({static_cast<int>(index), utf16Index});
    }
    return boundaries;
}

ComPtr<IDWriteTextFormat> createTextFormat(const std::string& fontFamily,
                                           float fontSize,
                                           int fontWeight,
                                           HorizontalAlign horizontalAlign,
                                           VerticalAlign verticalAlign,
                                           bool wrap) {
    ComPtr<IDWriteFactory>& factory = sharedDWriteFactory();
    if (factory == nullptr) {
        return {};
    }

    ComPtr<IDWriteTextFormat> format;
    const std::wstring family = resolvedFontFamily(fontFamily);
    if (FAILED(factory->CreateTextFormat(family.empty() ? L"Segoe UI" : family.c_str(),
                                         nullptr,
                                         effectiveFontWeight(fontFamily, fontWeight),
                                         DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL,
                                         std::max(1.0f, fontSize),
                                         L"",
                                         &format))) {
        return {};
    }

    format->SetTextAlignment(horizontalAlign == HorizontalAlign::Center
                                 ? DWRITE_TEXT_ALIGNMENT_CENTER
                                 : horizontalAlign == HorizontalAlign::Right
                                       ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                       : DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(verticalAlign == VerticalAlign::Center
                                      ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                      : verticalAlign == VerticalAlign::Bottom
                                            ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                            : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    return format;
}

ComPtr<IDWriteTextLayout> createTextLayout(const TextStyle& style) {
    ComPtr<IDWriteFactory>& factory = sharedDWriteFactory();
    if (factory == nullptr) {
        return {};
    }
    ComPtr<IDWriteTextFormat> format = createTextFormat(style.fontFamily,
                                                        style.fontSize,
                                                        style.fontWeight,
                                                        style.horizontalAlign,
                                                        style.verticalAlign,
                                                        style.wrap);
    if (format == nullptr) {
        return {};
    }

    const std::wstring wideText = utf8ToWide(style.text);
    const float maxWidth = style.maxWidth > 0.0f
        ? style.maxWidth
        : (style.wrap ? 4096.0f : std::numeric_limits<float>::max() / 4.0f);
    const float maxHeight = style.maxHeight > 0.0f
        ? style.maxHeight
        : (style.wrap ? 4096.0f : std::max(style.fontSize * 4.0f, 64.0f));

    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(wideText.c_str(),
                                         static_cast<UINT32>(wideText.size()),
                                         format.Get(),
                                         std::max(1.0f, maxWidth),
                                         std::max(1.0f, maxHeight),
                                         &layout))) {
        return {};
    }

    if (style.lineHeight > 0.0f) {
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                               style.lineHeight,
                               style.fontSize * 0.8f);
    }
    return layout;
}

TextPrimitive::TextMetrics buildTextMetrics(const TextStyle& style) {
    TextPrimitive::TextMetrics metrics;
    metrics.byteIndices.push_back(0);
    metrics.caretX.push_back(0.0f);
    if (style.text.empty()) {
        return metrics;
    }

    ComPtr<IDWriteTextLayout> layout = createTextLayout(style);
    if (layout == nullptr) {
        return metrics;
    }

    const std::vector<Utf8Boundary> boundaries = utf8Boundaries(style.text);
    metrics.byteIndices.clear();
    metrics.caretX.clear();
    for (const Utf8Boundary& boundary : boundaries) {
        FLOAT x = 0.0f;
        FLOAT y = 0.0f;
        DWRITE_HIT_TEST_METRICS hit{};
        layout->HitTestTextPosition(static_cast<UINT32>(boundary.utf16Index), FALSE, &x, &y, &hit);
        metrics.byteIndices.push_back(boundary.byteIndex);
        metrics.caretX.push_back(x);
    }

    DWRITE_TEXT_METRICS textMetrics{};
    layout->GetMetrics(&textMetrics);
    metrics.width = textMetrics.widthIncludingTrailingWhitespace;
    if (!metrics.caretX.empty()) {
        metrics.width = std::max(metrics.width, metrics.caretX.back());
    }
    return metrics;
}

} // namespace

struct TextPrimitive::Impl {
    TextStyle style_{};
    Vec2 position_{};
    Vec2 measuredSize_{};
    TransformMatrix transformMatrix_{};
    bool hasTransformMatrix_ = false;
    bool layoutDirty_ = true;

    bool initialize() { return true; }
    void destroy() {}

    void setPosition(float x, float y) { position_ = {x, y}; }
    void setText(const std::string& text) { style_.text = text; layoutDirty_ = true; }
    void setFontFamily(const std::string& fontFamily) { style_.fontFamily = fontFamily; layoutDirty_ = true; }
    void setFontSize(float fontSize) { style_.fontSize = std::max(1.0f, fontSize); layoutDirty_ = true; }
    void setFontWeight(int fontWeight) { style_.fontWeight = fontWeight; layoutDirty_ = true; }
    void setColor(const Color& color) { style_.color = color; }
    void setMaxWidth(float maxWidth) { style_.maxWidth = std::max(0.0f, maxWidth); layoutDirty_ = true; }
    void setMaxHeight(float maxHeight) { style_.maxHeight = std::max(0.0f, maxHeight); layoutDirty_ = true; }
    void setWrap(bool wrap) { style_.wrap = wrap; layoutDirty_ = true; }
    void setHorizontalAlign(HorizontalAlign align) { style_.horizontalAlign = align; layoutDirty_ = true; }
    void setVerticalAlign(VerticalAlign align) { style_.verticalAlign = align; layoutDirty_ = true; }
    void setLineHeight(float lineHeight) { style_.lineHeight = std::max(0.0f, lineHeight); layoutDirty_ = true; }
    void setStyle(const TextStyle& style) { style_ = style; layoutDirty_ = true; }
    void setVisualScale(float originX, float originY, float scale) {
        transformMatrix_ = {scale, 0.0f, originX * (1.0f - scale),
                            0.0f, scale, originY * (1.0f - scale),
                            0.0f, 0.0f, 1.0f};
        hasTransformMatrix_ = true;
    }
    void setTransform(const Transform& transform, const Rect& frame) {
        const Vec2 origin = {
            frame.x + frame.width * transform.origin.x,
            frame.y + frame.height * transform.origin.y
        };
        const float cosine = std::cos(transform.rotate);
        const float sine = std::sin(transform.rotate);
        transformMatrix_ = {
            transform.scale.x * cosine,
            -transform.scale.y * sine,
            origin.x + transform.translate.x - origin.x * transform.scale.x * cosine + origin.y * transform.scale.y * sine,
            transform.scale.x * sine,
            transform.scale.y * cosine,
            origin.y + transform.translate.y - origin.x * transform.scale.x * sine - origin.y * transform.scale.y * cosine,
            0.0f,
            0.0f,
            1.0f
        };
        hasTransformMatrix_ = true;
    }
    void setTransformMatrix(const TransformMatrix& matrix) {
        transformMatrix_ = matrix;
        hasTransformMatrix_ = true;
    }

    const TextStyle& style() const { return style_; }
    Vec2 position() const { return position_; }

    Vec2 measuredSize() {
        if (layoutDirty_) {
            prepare();
        }
        return measuredSize_;
    }

    static float measureTextWidth(const std::string& text,
                                  const std::string& fontFamily,
                                  float fontSize,
                                  int fontWeight) {
        TextStyle style;
        style.text = text;
        style.fontFamily = fontFamily;
        style.fontSize = fontSize;
        style.fontWeight = fontWeight;
        return buildTextMetrics(style).width;
    }

    static TextPrimitive::TextMetrics measureTextMetrics(const std::string& text,
                                                         const std::string& fontFamily,
                                                         float fontSize,
                                                         int fontWeight) {
        TextStyle style;
        style.text = text;
        style.fontFamily = fontFamily;
        style.fontSize = fontSize;
        style.fontWeight = fontWeight;
        return buildTextMetrics(style);
    }

    static void setDefaultFontFiles(const std::string& textFontFile, const std::string& iconFontFile) {
        defaultUiFontFileOverride() = textFontFile;
        defaultIconFontFileOverride() = iconFontFile;
        ensurePrivateFontRegistered(resolveDefaultUiFontPath());
        ensurePrivateFontRegistered(resolveDefaultIconFontPath());
    }

    void prepare() {
        ComPtr<IDWriteTextLayout> layout = createTextLayout(style_);
        if (layout == nullptr) {
            measuredSize_ = {};
            layoutDirty_ = false;
            return;
        }

        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        measuredSize_ = {metrics.widthIncludingTrailingWhitespace, metrics.height};
        layoutDirty_ = false;
    }

    void render(int windowWidth, int windowHeight) {
        if (layoutDirty_) {
            prepare();
        }
        if (style_.text.empty() || style_.color.a <= 0.001f) {
            return;
        }
        render::RenderBackend* backend = render::activeRenderBackend();
        if (backend == nullptr) {
            return;
        }

        render::TextDrawCommand command{};
        command.utf8Text = style_.text.c_str();
        command.fontFamily = style_.fontFamily.c_str();
        command.originX = position_.x;
        command.originY = position_.y;
        command.maxWidth = style_.maxWidth;
        command.maxHeight = style_.maxHeight > 0.0f
            ? style_.maxHeight
            : (measuredSize_.y > 0.0f ? measuredSize_.y : std::max(style_.fontSize * 2.0f, 32.0f));
        command.fontSize = style_.fontSize;
        command.lineHeight = style_.lineHeight;
        command.fontWeight = style_.fontWeight;
        command.wrap = style_.wrap;
        command.horizontalAlign = style_.horizontalAlign;
        command.verticalAlign = style_.verticalAlign;
        command.color = style_.color;
        command.transformMatrix = transformMatrix_;
        command.hasTransformMatrix = hasTransformMatrix_;
        backend->drawText(command, windowWidth, windowHeight);
    }
};

TextPrimitive::TextPrimitive()
    : impl_(std::make_unique<Impl>()) {}

TextPrimitive::TextPrimitive(float x, float y)
    : impl_(std::make_unique<Impl>()) {
    impl_->setPosition(x, y);
}

TextPrimitive::~TextPrimitive() = default;
TextPrimitive::TextPrimitive(TextPrimitive&&) noexcept = default;
TextPrimitive& TextPrimitive::operator=(TextPrimitive&&) noexcept = default;

bool TextPrimitive::initialize() { return impl_->initialize(); }
void TextPrimitive::destroy() { impl_->destroy(); }
void TextPrimitive::setPosition(float x, float y) { impl_->setPosition(x, y); }
void TextPrimitive::setText(const std::string& text) { impl_->setText(text); }
void TextPrimitive::setFontFamily(const std::string& fontFamily) { impl_->setFontFamily(fontFamily); }
void TextPrimitive::setFontSize(float fontSize) { impl_->setFontSize(fontSize); }
void TextPrimitive::setFontWeight(int fontWeight) { impl_->setFontWeight(fontWeight); }
void TextPrimitive::setColor(const Color& color) { impl_->setColor(color); }
void TextPrimitive::setMaxWidth(float maxWidth) { impl_->setMaxWidth(maxWidth); }
void TextPrimitive::setMaxHeight(float maxHeight) { impl_->setMaxHeight(maxHeight); }
void TextPrimitive::setWrap(bool wrap) { impl_->setWrap(wrap); }
void TextPrimitive::setHorizontalAlign(HorizontalAlign align) { impl_->setHorizontalAlign(align); }
void TextPrimitive::setVerticalAlign(VerticalAlign align) { impl_->setVerticalAlign(align); }
void TextPrimitive::setLineHeight(float lineHeight) { impl_->setLineHeight(lineHeight); }
void TextPrimitive::setStyle(const TextStyle& style) { impl_->setStyle(style); }
void TextPrimitive::setVisualScale(float originX, float originY, float scale) { impl_->setVisualScale(originX, originY, scale); }
void TextPrimitive::setTransform(const Transform& transform, const Rect& frame) { impl_->setTransform(transform, frame); }
void TextPrimitive::setTransformMatrix(const TransformMatrix& matrix) { impl_->setTransformMatrix(matrix); }
const TextStyle& TextPrimitive::style() const { return impl_->style(); }
Vec2 TextPrimitive::position() const { return impl_->position(); }
Vec2 TextPrimitive::measuredSize() { return impl_->measuredSize(); }
float TextPrimitive::measureTextWidth(const std::string& text,
                                      const std::string& fontFamily,
                                      float fontSize,
                                      int fontWeight) {
    return Impl::measureTextWidth(text, fontFamily, fontSize, fontWeight);
}
TextPrimitive::TextMetrics TextPrimitive::measureTextMetrics(const std::string& text,
                                                             const std::string& fontFamily,
                                                             float fontSize,
                                                             int fontWeight) {
    return Impl::measureTextMetrics(text, fontFamily, fontSize, fontWeight);
}
void TextPrimitive::setDefaultFontFiles(const std::string& textFontFile, const std::string& iconFontFile) {
    Impl::setDefaultFontFiles(textFontFile, iconFontFile);
}
void TextPrimitive::prepare() { impl_->prepare(); }
void TextPrimitive::render(int windowWidth, int windowHeight) { impl_->render(windowWidth, windowHeight); }

} // namespace core
