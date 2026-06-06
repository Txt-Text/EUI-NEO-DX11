#include "eui_neo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace app {
namespace {

struct LifecycleState {
    int frame = 0;
};

LifecycleState& state() {
    static LifecycleState value;
    return value;
}

eui::Color colorFromIndex(int index, float alpha = 1.0f) {
    const float hue = static_cast<float>((index * 47) % 360) / 360.0f;
    const float r = 0.40f + 0.35f * std::sin((hue + 0.00f) * 6.2831853f);
    const float g = 0.42f + 0.33f * std::sin((hue + 0.33f) * 6.2831853f);
    const float b = 0.46f + 0.32f * std::sin((hue + 0.66f) * 6.2831853f);
    return {std::clamp(r, 0.08f, 0.92f),
            std::clamp(g, 0.08f, 0.92f),
            std::clamp(b, 0.08f, 0.92f),
            alpha};
}

void drawBackground(eui::Ui& ui, float width, float height, int frame) {
    ui.rect("bg")
        .size(width, height)
        .gradient({0.045f, 0.052f, 0.064f, 1.0f},
                  {0.090f, 0.100f, 0.112f, 1.0f},
                  eui::GradientDirection::Vertical)
        .build();

    constexpr float cell = 42.0f;
    const int columns = static_cast<int>(std::ceil(width / cell));
    const int rows = static_cast<int>(std::ceil(height / cell));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            if (((x + y + frame / 24) % 4) == 0) {
                ui.rect("bg.cell." + std::to_string(x) + "." + std::to_string(y))
                    .x(static_cast<float>(x) * cell)
                    .y(static_cast<float>(y) * cell)
                    .size(cell, cell)
                    .color({1.0f, 1.0f, 1.0f, 0.018f})
                    .build();
            }
        }
    }
}

void drawPrimitiveChurn(eui::Ui& ui, int frame) {
    const int count = 42 + (frame / 90) % 18;
    for (int i = 0; i < count; ++i) {
        const float phase = static_cast<float>(frame) * 0.025f + static_cast<float>(i) * 0.21f;
        const float x = 34.0f + static_cast<float>(i % 14) * 62.0f;
        const float y = 78.0f + static_cast<float>(i / 14) * 58.0f + std::sin(phase) * 7.0f;
        ui.rect("primitive.churn." + std::to_string(i))
            .x(x)
            .y(y)
            .size(48.0f + std::sin(phase) * 6.0f, 38.0f + std::cos(phase) * 5.0f)
            .color(colorFromIndex(i, 0.72f))
            .radius(10.0f + static_cast<float>((i + frame / 12) % 12))
            .border(1.0f, {0.95f, 0.98f, 1.0f, 0.22f})
            .shadow(10.0f, 0.0f, 4.0f, {0.0f, 0.0f, 0.0f, 0.18f})
            .rotate(std::sin(phase) * 0.10f)
            .transformOrigin(0.5f, 0.5f)
            .build();
    }
}

void drawTextChurn(eui::Ui& ui, int frame, float width) {
    ui.text("title")
        .x(34.0f)
        .y(24.0f)
        .size(width - 68.0f, 36.0f)
        .text("Vulkan lifecycle stress  frame " + std::to_string(frame))
        .fontSize(26.0f)
        .lineHeight(34.0f)
        .color({0.92f, 0.96f, 1.0f, 1.0f})
        .build();

    const int rows = 9 + (frame / 120) % 5;
    for (int i = 0; i < rows; ++i) {
        const int glyphBase = (frame / 10 + i * 19) % 94;
        std::string sample = "atlas " + std::to_string(i) + "  ";
        for (int j = 0; j < 18; ++j) {
            sample.push_back(static_cast<char>(33 + (glyphBase + j * 7) % 94));
        }
        sample += "  图标文本混排";

        ui.text("text.row." + std::to_string(i))
            .x(34.0f)
            .y(274.0f + static_cast<float>(i) * 28.0f)
            .size(520.0f, 26.0f)
            .text(sample)
            .fontSize(17.0f + static_cast<float>((i + frame / 40) % 4))
            .lineHeight(24.0f)
            .color(colorFromIndex(i + 30, 0.94f))
            .build();

        ui.text("icon.row." + std::to_string(i))
            .x(568.0f + static_cast<float>(i % 3) * 40.0f)
            .y(274.0f + static_cast<float>(i) * 28.0f)
            .size(32.0f, 26.0f)
            .icon(0xF013 + static_cast<unsigned int>((i + frame / 30) % 8))
            .fontSize(19.0f)
            .lineHeight(24.0f)
            .color({0.88f, 0.92f, 1.0f, 0.86f})
            .build();
    }
}

void drawImageChurn(eui::Ui& ui, int frame, float width) {
    static const std::array<const char*, 3> sources = {
        "assets/icon.png",
        "assets/icon.svg",
        "assets/mona-loading-default.gif",
    };

    const int count = 36 + (frame / 75) % 24;
    const float startX = std::max(34.0f, width - 430.0f);
    for (int i = 0; i < count; ++i) {
        const int sourceIndex = (i + frame / 48) % static_cast<int>(sources.size());
        const float x = startX + static_cast<float>(i % 6) * 66.0f;
        const float y = 82.0f + static_cast<float>(i / 6) * 62.0f;
        const float pulse = std::sin(static_cast<float>(frame) * 0.035f + static_cast<float>(i) * 0.33f);
        ui.image("image.churn." + std::to_string(i))
            .source(sources[static_cast<std::size_t>(sourceIndex)])
            .x(x)
            .y(y + pulse * 4.0f)
            .size(52.0f, 52.0f)
            .radius(10.0f + static_cast<float>((i + frame / 20) % 14))
            .opacity(0.82f + pulse * 0.10f)
            .fit(i % 3 == 0 ? eui::ImageFit::Contain : eui::ImageFit::Cover)
            .rotate(pulse * 0.12f)
            .transformOrigin(0.5f, 0.5f)
            .build();
    }
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("EUI Vulkan Lifecycle")
        .pageId("vulkan-lifecycle")
        .clearColor({0.045f, 0.052f, 0.064f, 1.0f})
        .windowSize(1040, 720)
        .showDebugStatsInTitle(true);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    LifecycleState& lifecycle = state();

    ui.stack("frame-driver")
        .size(screen.width, screen.height)
        .onFrame([&](float) {
            ++lifecycle.frame;
        })
        .build();

    drawBackground(ui, screen.width, screen.height, lifecycle.frame);
    drawPrimitiveChurn(ui, lifecycle.frame);
    drawTextChurn(ui, lifecycle.frame, screen.width);
    drawImageChurn(ui, lifecycle.frame, screen.width);
}

} // namespace app
