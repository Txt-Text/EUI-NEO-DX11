#include "core/render/opengl/opengl_backend.h"

#include <glad/glad.h>

#include <utility>

namespace core::render::opengl {

OpenGLRenderBackend::OpenGLRenderBackend(Callback makeCurrent, Callback present)
    : makeCurrent_(std::move(makeCurrent)), present_(std::move(present)) {}

void OpenGLRenderBackend::makeCurrent() {
    if (makeCurrent_) {
        makeCurrent_();
    }
}

void OpenGLRenderBackend::beginFrame(int framebufferWidth, int framebufferHeight, float) {
    makeCurrent();
    glViewport(0, 0, framebufferWidth, framebufferHeight);
}

void OpenGLRenderBackend::present() {
    if (present_) {
        present_();
    }
}

} // namespace core::render::opengl
