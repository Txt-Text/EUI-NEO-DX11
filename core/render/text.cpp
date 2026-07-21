#include "core/render/text.h"

#include "core/render/render_backend.h"
#include "core/render/dx11/dx11_font_support.h"

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
#include <cstdint>
#include <limits>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
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

struct TextFormatCacheKey {
    std::string fontFamily;
    float fontSize = 0.0f;
    int fontWeight = 0;
    HorizontalAlign horizontalAlign = HorizontalAlign::Left;
    VerticalAlign verticalAlign = VerticalAlign::Top;
    bool wrap = false;

    bool operator==(const TextFormatCacheKey& other) const {
        return fontFamily == other.fontFamily &&
               fontSize == other.fontSize &&
               fontWeight == other.fontWeight &&
               horizontalAlign == other.horizontalAlign &&
               verticalAlign == other.verticalAlign &&
               wrap == other.wrap;
    }
};

struct TextLayoutCacheKey {
    TextFormatCacheKey formatKey;
    std::string text;
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;
    float lineHeight = 0.0f;

    bool operator==(const TextLayoutCacheKey& other) const {
        return formatKey == other.formatKey &&
               text == other.text &&
               maxWidth == other.maxWidth &&
               maxHeight == other.maxHeight &&
               lineHeight == other.lineHeight;
    }
};

template <typename Key>
std::size_t hashCombine(std::size_t seed, const Key& value) {
    return seed ^ (std::hash<Key>{}(value) + 0x9e3779b9u + (seed << 6) + (seed >> 2));
}

std::size_t hashTextFormatKey(const TextFormatCacheKey& key) {
    std::size_t seed = std::hash<std::string>{}(key.fontFamily);
    seed = hashCombine(seed, key.fontSize);
    seed = hashCombine(seed, key.fontWeight);
    seed = hashCombine(seed, static_cast<int>(key.horizontalAlign));
    seed = hashCombine(seed, static_cast<int>(key.verticalAlign));
    seed = hashCombine(seed, static_cast<int>(key.wrap));
    return seed;
}

std::size_t hashTextLayoutKey(const TextLayoutCacheKey& key) {
    std::size_t seed = hashTextFormatKey(key.formatKey);
    seed = hashCombine(seed, key.text);
    seed = hashCombine(seed, key.maxWidth);
    seed = hashCombine(seed, key.maxHeight);
    seed = hashCombine(seed, key.lineHeight);
    return seed;
}

struct CachedTextFormatEntry {
    TextFormatCacheKey key;
    ComPtr<IDWriteTextFormat> format;
};

struct CachedTextLayoutEntry {
    TextLayoutCacheKey key;
    ComPtr<IDWriteTextLayout> layout;
};

struct TextDWriteCaches {
    using TextFormatList = std::list<CachedTextFormatEntry>;
    using TextLayoutList = std::list<CachedTextLayoutEntry>;
    using TextFormatIndex = std::unordered_multimap<std::size_t, TextFormatList::iterator>;
    using TextLayoutIndex = std::unordered_multimap<std::size_t, TextLayoutList::iterator>;

    // 文本格式和布局都做进程内共享缓存，避免重复创建 DirectWrite 对象。
    std::mutex mutex;
    TextFormatList formatEntries;
    TextLayoutList layoutEntries;
    TextFormatIndex formatIndex;
    TextLayoutIndex layoutIndex;
};

void trimTextFormatCache(TextDWriteCaches& caches) {
    while (caches.formatEntries.size() > 256) {
        const auto oldest = caches.formatEntries.begin();
        const std::size_t hash = hashTextFormatKey(oldest->key);
        const auto range = caches.formatIndex.equal_range(hash);
        for (auto indexIt = range.first; indexIt != range.second; ++indexIt) {
            if (indexIt->second == oldest) {
                caches.formatIndex.erase(indexIt);
                break;
            }
        }
        caches.formatEntries.erase(oldest);
    }
}

void trimTextLayoutCache(TextDWriteCaches& caches) {
    while (caches.layoutEntries.size() > 256) {
        const auto oldest = caches.layoutEntries.begin();
        const std::size_t hash = hashTextLayoutKey(oldest->key);
        const auto range = caches.layoutIndex.equal_range(hash);
        for (auto indexIt = range.first; indexIt != range.second; ++indexIt) {
            if (indexIt->second == oldest) {
                caches.layoutIndex.erase(indexIt);
                break;
            }
        }
        caches.layoutEntries.erase(oldest);
    }
}

TextDWriteCaches& textDWriteCaches() {
    static TextDWriteCaches caches;
    return caches;
}

TextFormatCacheKey makeTextFormatCacheKey(const std::string& fontFamily,
                                          float fontSize,
                                          int fontWeight,
                                          HorizontalAlign horizontalAlign,
                                          VerticalAlign verticalAlign,
                                          bool wrap) {
    return {fontFamily, fontSize, fontWeight, horizontalAlign, verticalAlign, wrap};
}

TextLayoutCacheKey makeTextLayoutCacheKey(const TextStyle& style) {
    return {
        makeTextFormatCacheKey(style.fontFamily,
                               style.fontSize,
                               style.fontWeight,
                               style.horizontalAlign,
                               style.verticalAlign,
                               style.wrap),
        style.text,
        style.maxWidth > 0.0f
            ? style.maxWidth
            : (style.wrap ? 4096.0f : std::numeric_limits<float>::max() / 4.0f),
        style.maxHeight > 0.0f
            ? style.maxHeight
            : (style.wrap ? 4096.0f : std::max(style.fontSize * 4.0f, 64.0f)),
        style.lineHeight
    };
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

std::vector<Utf8Boundary> utf8Boundaries(const std::string& text) {
    // DirectWrite 命中测试使用 UTF-16 下标，这里保留 UTF-8 到 UTF-16 的边界映射。
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

    const TextFormatCacheKey key = makeTextFormatCacheKey(fontFamily,
                                                          fontSize,
                                                          fontWeight,
                                                          horizontalAlign,
                                                          verticalAlign,
                                                          wrap);
    const std::size_t keyHash = hashTextFormatKey(key);
    TextDWriteCaches& caches = textDWriteCaches();
    {
        std::scoped_lock lock(caches.mutex);
        const auto range = caches.formatIndex.equal_range(keyHash);
        for (auto it = range.first; it != range.second; ++it) {
            const CachedTextFormatEntry& entry = *it->second;
            if (entry.key == key && entry.format != nullptr) {
                ++core::render::currentRenderFrameStats().textFormatCacheHits;
                return entry.format;
            }
        }
    }

    ++core::render::currentRenderFrameStats().textFormatCacheMisses;
    ++core::render::currentRenderFrameStats().textFormatCreates;
    const render::dx11::FontSelection selection = render::dx11::candidateFontSelection(fontFamily.c_str());
    const Microsoft::WRL::ComPtr<IDWriteFontCollection> customCollection =
        render::dx11::cachedFontCollectionFromFile(factory.Get(), selection.fontFilePath);
    for (const std::wstring& family : selection.families) {
        ComPtr<IDWriteTextFormat> format;
        if (customCollection != nullptr &&
            SUCCEEDED(factory->CreateTextFormat(family.c_str(),
                                                customCollection.Get(),
                                                render::dx11::effectiveFontWeight(fontFamily, fontWeight),
                                                DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL,
                                                std::max(1.0f, fontSize),
                                                L"",
                                                &format))) {
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
            {
                std::scoped_lock lock(caches.mutex);
                caches.formatEntries.push_back({key, format});
                const auto entryIt = std::prev(caches.formatEntries.end());
                caches.formatIndex.emplace(keyHash, entryIt);
                trimTextFormatCache(caches);
            }
            return format;
        }
        if (SUCCEEDED(factory->CreateTextFormat(family.c_str(),
                                                nullptr,
                                                render::dx11::effectiveFontWeight(fontFamily, fontWeight),
                                                DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL,
                                                std::max(1.0f, fontSize),
                                                L"",
                                                &format))) {
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
            {
                std::scoped_lock lock(caches.mutex);
                caches.formatEntries.push_back({key, format});
                const auto entryIt = std::prev(caches.formatEntries.end());
                caches.formatIndex.emplace(keyHash, entryIt);
                trimTextFormatCache(caches);
            }
            return format;
        }
    }
    return {};
}

ComPtr<IDWriteTextLayout> createTextLayout(const TextStyle& style) {
    ComPtr<IDWriteFactory>& factory = sharedDWriteFactory();
    if (factory == nullptr) {
        return {};
    }

    const TextLayoutCacheKey key = makeTextLayoutCacheKey(style);
    const std::size_t keyHash = hashTextLayoutKey(key);
    TextDWriteCaches& caches = textDWriteCaches();
    {
        std::scoped_lock lock(caches.mutex);
        const auto range = caches.layoutIndex.equal_range(keyHash);
        for (auto it = range.first; it != range.second; ++it) {
            const CachedTextLayoutEntry& entry = *it->second;
            if (entry.key == key && entry.layout != nullptr) {
                ++core::render::currentRenderFrameStats().textLayoutCacheHits;
                return entry.layout;
            }
        }
    }

    ++core::render::currentRenderFrameStats().textLayoutCacheMisses;
    ComPtr<IDWriteTextFormat> format = createTextFormat(style.fontFamily,
                                                        style.fontSize,
                                                        style.fontWeight,
                                                        style.horizontalAlign,
                                                        style.verticalAlign,
                                                        style.wrap);
    if (format == nullptr) {
        return {};
    }

    const std::wstring wideText = render::dx11::utf8ToWide(style.text);
    const float maxWidth = key.maxWidth;
    const float maxHeight = key.maxHeight;

    ++core::render::currentRenderFrameStats().textLayoutCreates;
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
        // 行高在布局阶段一次性固化到 layout，渲染阶段直接复用。
        DWRITE_LINE_METRICS lineMetrics{};
        UINT32 actualLineCount = 0;
        const HRESULT metricsResult = layout->GetLineMetrics(&lineMetrics, 1, &actualLineCount);
        const float defaultLineHeight = (SUCCEEDED(metricsResult) && actualLineCount > 0 && lineMetrics.height > 0.0f)
            ? lineMetrics.height
            : style.fontSize;
        const float defaultBaseline = (SUCCEEDED(metricsResult) && actualLineCount > 0 && lineMetrics.baseline > 0.0f)
            ? lineMetrics.baseline
            : style.fontSize * 0.8f;
        const float baseline = defaultBaseline + (style.lineHeight - defaultLineHeight) * 0.5f;
        layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                               style.lineHeight,
                               std::clamp(baseline, 0.0f, style.lineHeight));
    }

    {
        std::scoped_lock lock(caches.mutex);
        caches.layoutEntries.push_back({key, layout});
        const auto entryIt = std::prev(caches.layoutEntries.end());
        caches.layoutIndex.emplace(keyHash, entryIt);
        trimTextLayoutCache(caches);
    }
    return layout;
}

TextPrimitive::TextMetrics buildTextMetricsFromLayout(const std::string& text,
                                                  IDWriteTextLayout* layout) {
    // caret stop 统一基于 DirectWrite layout 命中测试，避免维护第二套测量逻辑。
    TextPrimitive::TextMetrics metrics;
    metrics.byteIndices.push_back(0);
    metrics.caretX.push_back(0.0f);
    if (text.empty() || layout == nullptr) {
        return metrics;
    }

    const std::vector<Utf8Boundary> boundaries = utf8Boundaries(text);
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

TextPrimitive::TextMetrics buildTextMetrics(const TextStyle& style) {
    ComPtr<IDWriteTextLayout> layout = createTextLayout(style);
    return buildTextMetricsFromLayout(style.text, layout.Get());
}

} // namespace

struct TextPrimitive::Impl {
    TextStyle style_{};
    Vec2 position_{};
    Vec2 measuredSize_{};
    TransformMatrix transformMatrix_{};
    bool hasTransformMatrix_ = false;
    bool layoutDirty_ = true;
    bool paintDirty_ = true;
    ComPtr<IDWriteTextLayout> preparedLayout_;

    bool initialize() { return true; }
    void destroy() {}

    void markLayoutDirty() {
        // 文本内容、字体和排版参数变化时，布局与绘制结果都需要失效。
        layoutDirty_ = true;
        paintDirty_ = true;
        preparedLayout_.Reset();
    }

    void markPaintDirty() {
        paintDirty_ = true;
    }

    void setPosition(float x, float y) {
        if (position_.x == x && position_.y == y) {
            return;
        }
        position_ = {x, y};
        markPaintDirty();
    }

    void setText(const std::string& text) {
        if (style_.text == text) {
            return;
        }
        style_.text = text;
        markLayoutDirty();
    }

    void setFontFamily(const std::string& fontFamily) {
        if (style_.fontFamily == fontFamily) {
            return;
        }
        style_.fontFamily = fontFamily;
        markLayoutDirty();
    }

    void setFontSize(float fontSize) {
        const float normalized = std::max(1.0f, fontSize);
        if (style_.fontSize == normalized) {
            return;
        }
        style_.fontSize = normalized;
        markLayoutDirty();
    }

    void setFontWeight(int fontWeight) {
        if (style_.fontWeight == fontWeight) {
            return;
        }
        style_.fontWeight = fontWeight;
        markLayoutDirty();
    }

    void setColor(const Color& color) {
        if (style_.color.r == color.r && style_.color.g == color.g &&
            style_.color.b == color.b && style_.color.a == color.a) {
            return;
        }
        style_.color = color;
        markPaintDirty();
    }

    void setMaxWidth(float maxWidth) {
        const float normalized = std::max(0.0f, maxWidth);
        if (style_.maxWidth == normalized) {
            return;
        }
        style_.maxWidth = normalized;
        markLayoutDirty();
    }

    void setMaxHeight(float maxHeight) {
        const float normalized = std::max(0.0f, maxHeight);
        if (style_.maxHeight == normalized) {
            return;
        }
        style_.maxHeight = normalized;
        markLayoutDirty();
    }

    void setWrap(bool wrap) {
        if (style_.wrap == wrap) {
            return;
        }
        style_.wrap = wrap;
        markLayoutDirty();
    }

    void setHorizontalAlign(HorizontalAlign align) {
        if (style_.horizontalAlign == align) {
            return;
        }
        style_.horizontalAlign = align;
        markLayoutDirty();
    }

    void setVerticalAlign(VerticalAlign align) {
        if (style_.verticalAlign == align) {
            return;
        }
        style_.verticalAlign = align;
        markLayoutDirty();
    }

    void setLineHeight(float lineHeight) {
        const float normalized = std::max(0.0f, lineHeight);
        if (style_.lineHeight == normalized) {
            return;
        }
        style_.lineHeight = normalized;
        markLayoutDirty();
    }

    void setStyle(const TextStyle& style) {
        const TextStyle normalized = [&] {
            TextStyle result = style;
            result.fontSize = std::max(1.0f, result.fontSize);
            result.maxWidth = std::max(0.0f, result.maxWidth);
            result.maxHeight = std::max(0.0f, result.maxHeight);
            result.lineHeight = std::max(0.0f, result.lineHeight);
            return result;
        }();

        const bool layoutChanged =
            style_.text != normalized.text ||
            style_.fontFamily != normalized.fontFamily ||
            style_.fontSize != normalized.fontSize ||
            style_.fontWeight != normalized.fontWeight ||
            style_.maxWidth != normalized.maxWidth ||
            style_.maxHeight != normalized.maxHeight ||
            style_.wrap != normalized.wrap ||
            style_.horizontalAlign != normalized.horizontalAlign ||
            style_.verticalAlign != normalized.verticalAlign ||
            style_.lineHeight != normalized.lineHeight;
        const bool paintChanged =
            style_.color.r != normalized.color.r ||
            style_.color.g != normalized.color.g ||
            style_.color.b != normalized.color.b ||
            style_.color.a != normalized.color.a;

        if (!layoutChanged && !paintChanged) {
            return;
        }

        style_ = normalized;
        if (layoutChanged) {
            markLayoutDirty();
        } else if (paintChanged) {
            markPaintDirty();
        }
    }

    void setVisualScale(float originX, float originY, float scale) {
        transformMatrix_ = {scale, 0.0f, originX * (1.0f - scale),
                            0.0f, scale, originY * (1.0f - scale),
                            0.0f, 0.0f, 1.0f};
        hasTransformMatrix_ = true;
        markPaintDirty();
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
        markPaintDirty();
    }
    void setTransformMatrix(const TransformMatrix& matrix) {
        transformMatrix_ = matrix;
        hasTransformMatrix_ = true;
        markPaintDirty();
    }

    const TextStyle& style() const { return style_; }
    Vec2 position() const { return position_; }

    Vec2 measuredSize() {
        prepare();
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
        render::dx11::setDefaultFontFileOverrides(textFontFile, iconFontFile);
        render::dx11::ensurePrivateFontRegisteredCached(render::dx11::resolveDefaultUiFontPathCached());
        render::dx11::ensurePrivateFontRegisteredCached(render::dx11::resolveDefaultIconFontPathCached());
    }

    void prepare() {
        if (!layoutDirty_) {
            return;
        }

        // 这里只重建排版结果，不缓存位置、颜色和变换等每帧可变状态。
        preparedLayout_ = createTextLayout(style_);
        ++core::render::currentRenderFrameStats().textLayoutRebuilds;
        if (preparedLayout_ == nullptr) {
            measuredSize_ = {};
            layoutDirty_ = false;
            return;
        }

        DWRITE_TEXT_METRICS metrics{};
        preparedLayout_->GetMetrics(&metrics);
        measuredSize_ = {metrics.widthIncludingTrailingWhitespace, metrics.height};
        layoutDirty_ = false;
    }

    void render(int windowWidth, int windowHeight) {
        prepare();
        if (style_.text.empty() || style_.color.a <= 0.001f) {
            return;
        }
        render::RenderBackend* backend = render::activeRenderBackend();
        if (backend == nullptr) {
            return;
        }

        // 绘制命令只携带当前帧可变状态，具体排版结果复用 preparedLayout_。
        render::TextDrawCommand command{};
        command.utf8Text = style_.text.c_str();
        command.fontFamily = style_.fontFamily.c_str();
        command.preparedLayout = preparedLayout_.Get();
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
        paintDirty_ = false;
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