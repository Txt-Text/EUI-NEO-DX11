#pragma once

#include "core/render/render_backend.h"

#include <functional>

namespace core::render::opengl {

class OpenGLRenderBackend final : public RenderBackend {
public:
    using Callback = std::function<void()>;

    OpenGLRenderBackend(Callback makeCurrent, Callback present);

    void makeCurrent() override;
    void beginFrame(int framebufferWidth, int framebufferHeight, float dpiScale) override;
    void present() override;

private:
    Callback makeCurrent_;
    Callback present_;
};

} // namespace core::render::opengl
