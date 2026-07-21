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

void addTextRow(eui::Ui& ui, const std::string& id, float width, int index) {
    const bool even = (index % 2) == 0;
    const eui::Color bg = even ? color(0.97f, 0.98f, 0.99f) : color(0.94f, 0.96f, 0.98f);
    const eui::Color accent = (index % 3) == 0 ? color(0.16f, 0.44f, 0.84f) : color(0.20f, 0.56f, 0.48f);

    ui.stack(id)
        .size(width, 58.0f)
        .content([&] {
            ui.rect(id + ".bg")
                .size(width, 58.0f)
                .radius(8.0f)
                .color(bg)
                .border(1.0f, color(accent.r, accent.g, accent.b, 0.18f))
                .build();

            ui.rect(id + ".accent")
                .x(14.0f)
                .y(10.0f)
                .size(4.0f, 38.0f)
                .radius(2.0f)
                .color(accent)
                .build();

            ui.text(id + ".title")
                .x(28.0f)
                .y(8.0f)
                .size(std::max(160.0f, width - 180.0f), 22.0f)
                .text("static text row " + std::to_string(index + 1) + " - cache steady-state probe")
                .fontSize(15.0f)
                .lineHeight(20.0f)
                .color(color(0.10f, 0.12f, 0.16f))
                .verticalAlign(eui::VerticalAlign::Center)
                .build();

            ui.text(id + ".meta")
                .x(28.0f)
                .y(30.0f)
                .size(std::max(160.0f, width - 180.0f), 18.0f)
                .text("watch title stats: prepare can stay nonzero while rebuilds, TF miss, TL miss should settle")
                .fontSize(12.0f)
                .lineHeight(16.0f)
                .color(color(0.40f, 0.45f, 0.52f))
                .verticalAlign(eui::VerticalAlign::Center)
                .build();

            ui.text(id + ".chip")
                .x(std::max(220.0f, width - 122.0f))
                .y(16.0f)
                .size(92.0f, 24.0f)
                .text("steady")
                .fontSize(12.0f)
                .lineHeight(16.0f)
                .horizontalAlign(eui::HorizontalAlign::Center)
                .verticalAlign(eui::VerticalAlign::Center)
                .color(accent)
                .build();
        })
        .build();
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Static Text List Viewer")
        .pageId("static_text_list_viewer")
        .clearColor(color(0.92f, 0.94f, 0.96f))
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
                .gradient(color(0.92f, 0.94f, 0.96f), color(0.97f, 0.96f, 0.93f), eui::GradientDirection::Vertical)
                .build();

            ui.text("title")
                .x(margin)
                .y(top)
                .size(viewportW, 34.0f)
                .text("Static Text List Viewer")
                .fontSize(28.0f)
                .lineHeight(32.0f)
                .fontWeight(700)
                .color(color(0.08f, 0.10f, 0.14f))
                .build();

            ui.text("subtitle")
                .x(margin)
                .y(top + 42.0f)
                .size(viewportW, 22.0f)
                .text("Idle on this screen after first paint. Text layout rebuild and cache miss counters should converge toward zero.")
                .fontSize(14.0f)
                .lineHeight(18.0f)
                .color(color(0.36f, 0.40f, 0.46f))
                .build();

            components::scrollView(ui, "static.text.scroll")
                .position(margin, viewportY)
                .size(viewportW, viewportH)
                .offset(viewer.scroll.get())
                .gap(8.0f)
                .step(58.0f)
                .scrollbarWidth(10.0f)
                .scrollbarGap(14.0f)
                .onChange([&viewer](float value) {
                    viewer.scroll.set(value);
                })
                .content([&](eui::Ui& contentUi, float contentWidth, float) {
                    for (int i = 0; i < 72; ++i) {
                        addTextRow(contentUi, "row." + std::to_string(i), contentWidth, i);
                    }
                })
                .build();
        })
        .build();
}

} // namespace app
