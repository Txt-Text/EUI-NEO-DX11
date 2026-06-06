#pragma once

#include "eui/app.h"
#include "core/app/app_runner.h"
#include "core/render/render_backend.h"
#include "core/render/render_surface.h"
#include "core/window/window_backend.h"

#include <chrono>
#include <utility>

namespace app {

struct MainWindowMetrics {
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    float dpiScale = 1.0f;
    float pointerScale = 1.0f;

    bool valid() const {
        return framebufferWidth > 0 && framebufferHeight > 0 && dpiScale > 0.0f && pointerScale > 0.0f;
    }
};

class MainWindowRuntime {
public:
    explicit MainWindowRuntime(AppRunner& runner) : runner_(runner) {}

    template <typename AfterUpdateFn, typename UpdateChildrenFn, typename SetTitleFn, typename ChildAnimatingFn>
    void runFrame(core::window::Handle window,
                  core::render::RenderBackend& renderBackend,
                  const MainWindowMetrics& metrics,
                  double now,
                  double refreshRate,
                  bool inputEnabled,
                  AfterUpdateFn&& afterUpdate,
                  UpdateChildrenFn&& updateChildren,
                  SetTitleFn&& setTitle,
                  ChildAnimatingFn&& childAnimating) {
        runner_.updateFrameInterval(refreshRate, now);
        const float deltaSeconds = runner_.consumeFrameDelta(now);
        const bool externalReady = runner_.consumeExternalReady();

        updateAndRender(window,
                        renderBackend,
                        metrics,
                        deltaSeconds,
                        externalReady,
                        inputEnabled,
                        std::forward<AfterUpdateFn>(afterUpdate));

        updateChildren(deltaSeconds, externalReady);
        runner_.updateFrameTitle(core::window::timeSeconds(), std::forward<SetTitleFn>(setTitle));
        runner_.advanceFrameClock(core::window::timeSeconds(), runner_.anyAnimating(childAnimating()));
    }

    void markUnavailableFrame(double now) {
        runner_.needsRender = true;
        runner_.resetTiming(now);
    }

    template <typename AfterUpdateFn>
    bool updateAndRender(core::window::Handle window,
                         core::render::RenderBackend& renderBackend,
                         const MainWindowMetrics& metrics,
                         float deltaSeconds,
                         bool externalReady,
                         bool inputEnabled,
                         AfterUpdateFn&& afterUpdate) {
        if (!metrics.valid()) {
            runner_.needsRender = true;
            return false;
        }

        renderBackend.makeCurrent();
        if (app::update(window,
                        deltaSeconds,
                        metrics.framebufferWidth,
                        metrics.framebufferHeight,
                        metrics.dpiScale,
                        metrics.pointerScale,
                        externalReady,
                        inputEnabled)) {
            runner_.needsRender = true;
        }

        afterUpdate();

        if (!runner_.needsRender) {
            return false;
        }

        const auto renderStart = std::chrono::steady_clock::now();
        renderBackend.beginFrame({
            window,
            core::window::nativeWindowInfo(window),
            metrics.framebufferWidth,
            metrics.framebufferHeight,
            metrics.dpiScale
        });
        core::render::ScopedRenderBackend scopedRenderBackend(renderBackend);
        app::render(metrics.framebufferWidth, metrics.framebufferHeight, metrics.dpiScale);
        renderBackend.present();
        const auto renderEnd = std::chrono::steady_clock::now();
        runner_.recordRenderDuration(std::chrono::duration<double, std::milli>(renderEnd - renderStart).count());
        runner_.markRendered();
        return true;
    }

private:
    AppRunner& runner_;
};

} // namespace app
