#include "core/render/render_backend.h"

#include "core/render/dx11/dx11_backend.h"

namespace core::render {

void initializeRenderBackendLoader() {}

core::window::RenderApi windowRenderApi() {
    return core::window::RenderApi::Dx11;
}

std::unique_ptr<RenderBackend> createRenderBackend(core::window::Handle window, RenderBackend* shareBackend) {
    (void)shareBackend;
    return std::make_unique<Dx11Backend>(window);
}

} // namespace core::render
