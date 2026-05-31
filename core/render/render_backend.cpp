#include "core/render/render_backend.h"

#if defined(EUI_RENDER_BACKEND_OPENGL)
#include "core/render/opengl/opengl_backend.h"
#elif defined(EUI_RENDER_BACKEND_VULKAN)
#include "core/render/vulkan/vulkan_backend.h"
#endif

namespace core::render {

core::window::RenderApi windowRenderApi() {
#if defined(EUI_RENDER_BACKEND_VULKAN)
    return core::window::RenderApi::Vulkan;
#else
    return core::window::RenderApi::OpenGL;
#endif
}

std::unique_ptr<RenderBackend> createRenderBackend(core::window::Handle window, RenderBackend* shareBackend) {
#if defined(EUI_RENDER_BACKEND_OPENGL)
    return std::make_unique<opengl::OpenGLRenderBackend>(window, shareBackend);
#elif defined(EUI_RENDER_BACKEND_VULKAN)
    return std::make_unique<vulkan::VulkanRenderBackend>(window, shareBackend);
#else
    (void)window;
    (void)shareBackend;
    return {};
#endif
}

} // namespace core::render
