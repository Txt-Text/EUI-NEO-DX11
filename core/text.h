#pragma once

#include "core/primitive.h"

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core {

enum class HorizontalAlign {
    Left,
    Center,
    Right
};

enum class VerticalAlign {
    Top,
    Center,
    Bottom
};

struct TextStyle {
    std::string text;
    std::string fontFamily;
    float fontSize = 16.0f;
    int fontWeight = 400;
    Color color = {1.0f, 1.0f, 1.0f, 1.0f};
    float maxWidth = 0.0f;
    bool wrap = false;
    HorizontalAlign horizontalAlign = HorizontalAlign::Left;
    VerticalAlign verticalAlign = VerticalAlign::Top;
    float lineHeight = 0.0f;
};

class TextPrimitive {
public:
    struct Glyph {
        float advance = 0.0f;
        float xOffset = 0.0f;
        float yOffset = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
        bool colored = false;
    };

    struct ShapedGlyph {
        std::uint64_t key = 0;
        unsigned int codepoint = 0;
        int byteStart = 0;
        int byteEnd = 0;
        float advance = 0.0f;
        float xOffset = 0.0f;
        float yOffset = 0.0f;
    };

    struct TextMetrics {
        float width = 0.0f;
        std::vector<int> byteIndices;
        std::vector<float> caretX;
    };

    TextPrimitive() = default;
    TextPrimitive(float x, float y) : position_{x, y} {}

    bool initialize();
    void destroy();

    void setPosition(float x, float y);
    void setText(const std::string& text);
    void setFontFamily(const std::string& fontFamily);
    void setFontSize(float fontSize);
    void setFontWeight(int fontWeight);
    void setColor(const Color& color);
    void setMaxWidth(float maxWidth);
    void setWrap(bool wrap);
    void setHorizontalAlign(HorizontalAlign align);
    void setVerticalAlign(VerticalAlign align);
    void setLineHeight(float lineHeight);
    void setStyle(const TextStyle& style);
    void setVisualScale(float originX, float originY, float scale);
    void setTransform(const Transform& transform, const Rect& frame);

    const TextStyle& style() const;
    Vec2 position() const;
    Vec2 measuredSize();
    static float measureTextWidth(const std::string& text,
                                  const std::string& fontFamily = {},
                                  float fontSize = 16.0f,
                                  int fontWeight = 400);
    static TextMetrics measureTextMetrics(const std::string& text,
                                          const std::string& fontFamily = {},
                                          float fontSize = 16.0f,
                                          int fontWeight = 400);
    static void setDefaultFontFiles(const std::string& textFontFile, const std::string& iconFontFile);

    void render(int windowWidth, int windowHeight);

private:
    struct LaidOutGlyph {
        Glyph glyph;
        float x = 0.0f;
        float y = 0.0f;
    };

    struct Line {
        std::vector<LaidOutGlyph> glyphs;
        float width = 0.0f;
    };

    bool loadFont();
    bool ensureGlyph(const ShapedGlyph& shaped);
    Glyph* findGlyph(std::uint64_t key);
    void cacheGlyph(std::uint64_t key, const Glyph& glyph);
    void invalidateLayout();
    void invalidateVertices();
    void rebuildLayout();
    void rebuildVertices();
    std::vector<ShapedGlyph> shapeText(const std::string& text);
    void appendShapedGlyphToLine(Line& line, const ShapedGlyph& shaped, float& cursorX);

    static unsigned int readCodepoint(const std::string& text, size_t& index);
    static std::string resolveFontPath(const std::string& fontFamily, int fontWeight);
    static GLuint compileShader(GLenum type, const char* source);

    Vec2 position_;
    Vec2 visualScaleOrigin_;
    float visualScale_ = 1.0f;
    Transform transform_;
    Rect transformFrame_;
    TextStyle style_;
    std::shared_ptr<void> fontInfoStorage_;
    float scale_ = 1.0f;
    float ascent_ = 0.0f;
    float descent_ = 0.0f;
    float lineGap_ = 0.0f;

    std::unordered_map<std::uint64_t, Glyph> glyphs_;

    std::vector<Line> lines_;
    std::vector<float> vertices_;
    Vec2 measuredSize_;
    bool layoutDirty_ = true;
    bool verticesDirty_ = true;
    bool fontDirty_ = true;

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint shaderProgram_ = 0;
    GLint windowSizeLocation_ = -1;
    GLint colorLocation_ = -1;
    GLint textureLocation_ = -1;
};

} // namespace core
