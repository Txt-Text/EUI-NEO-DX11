#pragma once

namespace core::render {

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    virtual void makeCurrent() = 0;
    virtual void beginFrame(int framebufferWidth, int framebufferHeight, float dpiScale) = 0;
    virtual void present() = 0;
};

} // namespace core::render
