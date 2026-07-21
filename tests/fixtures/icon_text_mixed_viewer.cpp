#include "eui_neo.h"

#include <algorithm>
#include <string>

namespace app {
namespace {

struct ViewerState {
    eui::Signal<float> scroll{0.0f};
};

ViewerState& state() {
    static ViewerState value;
    return value;
}

eui::Color color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

void mixedRow(eui::Ui& ui, const std::string& id, float width, int index) {
    const bool even = (index % 2) == 0;
    const eui::Color bg = even ? color(0.10f, 0.12f, 0.16f) : color(0.12f, 0.15f, 0.20f);
    const eui::Color border = even ? color(0.26f, 0.34f, 0.46f) : color(0.20f, 0.48f, 0.58f);
    const std::string icon = (index % 4) == 0 ? "\xEF\x80\x95" : (index % 4) == 1 ? "\xEF\x81\x98" : (index % 4) == 2 ? "\xEF\x81\xBB" : "\xEF\x87\xAB";

    ui.stack(id)
        .size(width, 64.0f)
        .content([&] {
            ui.rect(id + ".bg")
                .size(width, 64.0f)
                .radius(10.0f)
                .color(bg)
                .border(1.0f, color(border.r, border.g, border.b, 0.70f))
                .build();

            ui.text(id + ".icon")
                .x(18.0f)
                .y(12.0f)
                .size(28.0f, 40.0f)
                .text(icon)
                .fontFamily("FontAwesome")
                .fontSize(20.0f)
                .lineHeight(24.0f)
                .horizontalAlign(eui::HorizontalAlign::Center)
                .verticalAlign(eui::VerticalAlign::Center)
                .color(color(0.46f, 0.76f, 1.0f))
                .build();

            ui.text(id + ".title")
                .x(58.0f)
                .y(11.0f)
                .size(std::max(180.0f, width - 170.0f), 22.0f)
                .text("icon + text row " + std::to_string(index + 1) + "  mix FontAwesome with UI text")
                .fontSize(15.0f)
                .lineHeight(20.0f)
                .color(color(0.93f, 0.96f, 1.0f))
                .verticalAlign(eui::VerticalAlign::Center)
                .build();

            ui.text(id + ".meta")
                .x(58.0f)
                .y(33.0f)
                .size(std::max(180.0f, width - 170.0f), 18.0f)
                .text("font resolve, registration, collection and layout cache should stabilize after warmup")
                .fontSize(12.0f)
                .lineHeight(16.0f)
                .color(color(0.62f, 0.69f, 0.78f))
                .verticalAlign(eui::VerticalAlign::Center)
                .build();

            ui.text(id + ".tag")
                .x(std::max(220.0f, width - 108.0f))
                .y(20.0f)
                .size(76.0f, 22.0f)
                .text((index % 3) == 0 ? "alerts" : (index % 3) == 1 ? "tools" : "nav")
                .fontSize(11.0f)
                .lineHeight(14.0f)
                .horizontalAlign(eui::HorizontalAlign::Center)
                .verticalAlign(eui::VerticalAlign::Center)
                .color(color(0.60f, 0.84f, 1.0f))
                .build();
        })
        .build();
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Icon Text Mixed Viewer")
        .pageId("icon_text_mixed_viewer")
        .clearColor(color(0.07f, 0.08f, 0.10f))
        .windowSize(980, 760)
        .showDebugStatsInTitle(true)
        .fps(120.0)
        .iconPath("");
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    ViewerState& viewer = state();
    const float margin = std::clamp(screen.width * 0.05f, 28.0f, 56.0f);
    const float top = 26.0f;
    const float viewportY = 106.0f;
    const float viewportW = std::max(420.0f, screen.width - margin * 2.0f);
    const float viewportH = std::max(240.0f, screen.height - viewportY - 26.0f);

    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.rect("background")
                .size(screen.width, screen.height)
                .gradient(color(0.07f, 0.08f, 0.10f), color(0.10f, 0.11f, 0.15f), eui::GradientDirection::Vertical)
                .build();

            ui.text("title")
                .x(margin)
                .y(top)
                .size(viewportW, 34.0f)
                .text("Icon Text Mixed Viewer")
                .fontSize(28.0f)
                .lineHeight(32.0f)
                .fontWeight(700)
                .color(color(0.95f, 0.97f, 1.0f))
                .build();

            ui.text("subtitle")
                .x(margin)
                .y(top + 42.0f)
                .size(viewportW, 22.0f)
                .text("Use this to verify FontAwesome registration/path resolution settles into cache-hit steady-state.")
                .fontSize(14.0f)
                .lineHeight(18.0f)
                .color(color(0.62f, 0.69f, 0.78f))
                .build();

            components::scrollView(ui, "icon.text.scroll")
                .position(margin, viewportY)
                .size(viewportW, viewportH)
                .offset(viewer.scroll.get())
                .gap(8.0f)
                .step(64.0f)
                .scrollbarWidth(10.0f)
                .scrollbarGap(14.0f)
                .onChange([&viewer](float value) {
                    viewer.scroll.set(value);
                })
                .content([&](eui::Ui& contentUi, float contentWidth, float) {
                    for (int i = 0; i < 64; ++i) {
                        mixedRow(contentUi, "row." + std::to_string(i), contentWidth, i);
                    }
                })
                .build();
        })
        .build();
}

} // namespace app
