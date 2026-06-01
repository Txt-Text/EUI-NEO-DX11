#include "core/render/opengl/opengl_backend.h"

#include "core/render/render_types.h"

#include <algorithm>
#include <cstdint>

#include <glad/glad.h>

namespace core::render::opengl {

namespace {

struct ImageTextureResource {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
};

GLuint textureIdFromHandle(RenderBackend::TextureHandle handle) {
    auto* resource = static_cast<ImageTextureResource*>(handle);
    return resource != nullptr ? resource->texture : 0;
}

} // namespace

OpenGLRenderBackend::TextureHandle OpenGLRenderBackend::createTexture(const unsigned char* pixels,
                                                                      int width,
                                                                      int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    auto* resource = new ImageTextureResource();
    glGenTextures(1, &resource->texture);
    if (resource->texture == 0) {
        delete resource;
        return nullptr;
    }

    resource->width = width;
    resource->height = height;
    glBindTexture(GL_TEXTURE_2D, resource->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return resource;
}

bool OpenGLRenderBackend::updateTexture(TextureHandle handle, const unsigned char* pixels, int width, int height) {
    auto* resource = static_cast<ImageTextureResource*>(handle);
    if (resource == nullptr || resource->texture == 0 || pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, resource->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (resource->width != width || resource->height != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        resource->width = width;
        resource->height = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void OpenGLRenderBackend::destroyTexture(TextureHandle handle) {
    auto* resource = static_cast<ImageTextureResource*>(handle);
    if (resource == nullptr) {
        return;
    }
    if (resource->texture != 0) {
        glDeleteTextures(1, &resource->texture);
        resource->texture = 0;
    }
    delete resource;
}

void OpenGLRenderBackend::drawTexture(TextureHandle handle,
                                      const float* vertices,
                                      std::size_t vertexFloatCount,
                                      const core::Color& tint,
                                      const core::Rect& rect,
                                      float radius,
                                      int windowWidth,
                                      int windowHeight) {
    const GLuint texture = textureIdFromHandle(handle);
    if (texture == 0 || vertices == nullptr || vertexFloatCount < 42 ||
        tint.a <= 0.001f || windowWidth <= 0 || windowHeight <= 0) {
        return;
    }
    if (!ensureImageResources()) {
        return;
    }

    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(imageShaderProgram_);
    glUniform2f(imageWindowSizeLocation_, static_cast<float>(std::max(1, windowWidth)),
                static_cast<float>(std::max(1, windowHeight)));
    glUniform4f(imageTintLocation_, tint.r, tint.g, tint.b, tint.a);
    glUniform4f(imageRectLocation_, rect.x, rect.y, rect.width, rect.height);
    glUniform1f(imageRadiusLocation_, radius);
    glUniform1i(imageTextureLocation_, 0);

    glBindVertexArray(imageVao_);
    glBindBuffer(GL_ARRAY_BUFFER, imageVbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(vertexFloatCount * sizeof(float)), vertices);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexFloatCount / 7));
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (!blendEnabled) {
        glDisable(GL_BLEND);
    }
}

bool OpenGLRenderBackend::ensureImageResources() {
    if (imageShaderProgram_ != 0 && imageVao_ != 0 && imageVbo_ != 0) {
        return true;
    }

    const char* vertexSource =
        "#version 330 core\n"
        "layout(location = 0) in vec3 aScreenPos;\n"
        "layout(location = 1) in vec2 aLocalPos;\n"
        "layout(location = 2) in vec2 aUV;\n"
        "uniform vec2 uWindowSize;\n"
        "out vec2 vLocalPos;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "    vLocalPos = aLocalPos;\n"
        "    vUV = aUV;\n"
        "    vec2 ndc = vec2((aScreenPos.x / uWindowSize.x) * 2.0 - 1.0,\n"
        "                    1.0 - (aScreenPos.y / uWindowSize.y) * 2.0);\n"
        "    gl_Position = vec4(ndc * aScreenPos.z, 0.0, aScreenPos.z);\n"
        "}\n";

    const char* fragmentSource =
        "#version 330 core\n"
        "in vec2 vLocalPos;\n"
        "in vec2 vUV;\n"
        "out vec4 FragColor;\n"
        "uniform sampler2D uTexture;\n"
        "uniform vec4 uTint;\n"
        "uniform vec4 uRect;\n"
        "uniform float uRadius;\n"
        "float roundedBoxDistance(vec2 point, vec2 halfSize, float radius) {\n"
        "    vec2 cornerVector = abs(point) - halfSize + vec2(radius);\n"
        "    return length(max(cornerVector, 0.0)) + min(max(cornerVector.x, cornerVector.y), 0.0) - radius;\n"
        "}\n"
        "void main() {\n"
        "    vec2 center = uRect.xy + uRect.zw * 0.5;\n"
        "    float distanceToEdge = roundedBoxDistance(vLocalPos - center, uRect.zw * 0.5, uRadius);\n"
        "    float edgeWidth = max(fwidth(distanceToEdge), 0.75);\n"
        "    float shapeAlpha = 1.0 - smoothstep(-edgeWidth, edgeWidth, distanceToEdge);\n"
        "    if (shapeAlpha <= 0.0) discard;\n"
        "    vec4 sampled = texture(uTexture, vUV);\n"
        "    FragColor = vec4(sampled.rgb * uTint.rgb, sampled.a * uTint.a * shapeAlpha);\n"
        "}\n";

    const GLuint vertexShader = compileImageShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compileImageShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader != 0) {
            glDeleteShader(fragmentShader);
        }
        return false;
    }

    imageShaderProgram_ = glCreateProgram();
    glAttachShader(imageShaderProgram_, vertexShader);
    glAttachShader(imageShaderProgram_, fragmentShader);
    glLinkProgram(imageShaderProgram_);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = 0;
    glGetProgramiv(imageShaderProgram_, GL_LINK_STATUS, &linked);
    if (!linked) {
        releaseImageResources();
        return false;
    }

    imageWindowSizeLocation_ = glGetUniformLocation(imageShaderProgram_, "uWindowSize");
    imageTextureLocation_ = glGetUniformLocation(imageShaderProgram_, "uTexture");
    imageTintLocation_ = glGetUniformLocation(imageShaderProgram_, "uTint");
    imageRectLocation_ = glGetUniformLocation(imageShaderProgram_, "uRect");
    imageRadiusLocation_ = glGetUniformLocation(imageShaderProgram_, "uRadius");

    glGenVertexArrays(1, &imageVao_);
    glGenBuffers(1, &imageVbo_);
    glBindVertexArray(imageVao_);
    glBindBuffer(GL_ARRAY_BUFFER, imageVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 42, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 7, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 7, reinterpret_cast<void*>(sizeof(float) * 3));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 7, reinterpret_cast<void*>(sizeof(float) * 5));
    glEnableVertexAttribArray(2);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return imageShaderProgram_ != 0 && imageVao_ != 0 && imageVbo_ != 0;
}

unsigned int OpenGLRenderBackend::compileImageShader(unsigned int type, const char* source) const {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void OpenGLRenderBackend::releaseImageResources() {
    if (imageVbo_ != 0) {
        glDeleteBuffers(1, &imageVbo_);
        imageVbo_ = 0;
    }
    if (imageVao_ != 0) {
        glDeleteVertexArrays(1, &imageVao_);
        imageVao_ = 0;
    }
    if (imageShaderProgram_ != 0) {
        glDeleteProgram(imageShaderProgram_);
        imageShaderProgram_ = 0;
    }
    imageWindowSizeLocation_ = -1;
    imageTextureLocation_ = -1;
    imageTintLocation_ = -1;
    imageRectLocation_ = -1;
    imageRadiusLocation_ = -1;
}

} // namespace core::render::opengl
