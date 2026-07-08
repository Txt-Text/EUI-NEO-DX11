#include "eui_neo.h"

namespace app {
namespace {

struct DemoState {
    bool dark = true;
    bool pinned = false;
    int counter = 0;
};

DemoState state;

bool titleBarButtonsNeedSync() {
    static bool initialized = false;
    static bool lastDark = false;
    static bool lastPinned = false;
    if (!initialized || lastDark != state.dark || lastPinned != state.pinned) {
        initialized = true;
        lastDark = state.dark;
        lastPinned = state.pinned;
        return true;
    }
    return false;
}

void syncTitleBarButtons() {
    if (!titleBarButtonsNeedSync()) {
        return;
    }

    clearTitleBarButtons();
    setTitleBarSystemButtons(true, true, true);

    registerTitleBarButton(TitleBarButton{
        .id = "add",
        .icon = core::dsl::utf8(0xF067),
        .onClick = [] {
            ++state.counter;
        },
        .visible = {},
        .active = {},
        .placement = TitleBarButtonPlacement::Left,
    });

    registerTitleBarButton(TitleBarButton{
        .id = "theme",
        .icon = state.dark ? core::dsl::utf8(0xF185) : core::dsl::utf8(0xF186),
        .onClick = [] {
            state.dark = !state.dark;
        },
        .visible = {},
        .active = [] {
            return state.dark;
        },
        .placement = TitleBarButtonPlacement::Right,
    });

    registerTitleBarButton(TitleBarButton{
        .id = "pin",
        .icon = core::dsl::utf8(0xF08D),
        .onClick = [] {
            state.pinned = !state.pinned;
        },
        .visible = {},
        .active = [] {
            return state.pinned;
        },
        .placement = TitleBarButtonPlacement::Right,
    });
}

components::theme::ThemeColorTokens tokens() {
    return state.dark ? components::theme::dark() : components::theme::light();
}

eui::Color backgroundColor() {
    return state.dark ? eui::Color{0.08f, 0.09f, 0.11f, 1.0f} : eui::Color{0.95f, 0.96f, 0.98f, 1.0f};
}

eui::Color panelColor() {
    return state.dark ? eui::Color{0.12f, 0.13f, 0.16f, 1.0f} : eui::Color{1.0f, 1.0f, 1.0f, 1.0f};
}

eui::Color borderColor() {
    return state.dark ? eui::Color{0.22f, 0.24f, 0.28f, 1.0f} : eui::Color{0.84f, 0.86f, 0.90f, 1.0f};
}

const char* modeLabel() {
    return state.dark ? "Dark" : "Light";
}

const char* pinLabel() {
    return state.pinned ? "Pinned" : "Normal";
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static DslAppConfig config = DslAppConfig{}
        .title("Title Bar Buttons")
        .pageId("title_bar_buttons")
        .customTitleBar(true)
        .iconFont("Font Awesome 7 Free-Solid-900.otf")
        .windowSize(960, 640);
    config.clearColor(backgroundColor());
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    syncTitleBarButtons();

    const auto theme = tokens();
    const eui::Color bg = backgroundColor();
    const eui::Color panel = panelColor();
    const eui::Color border = borderColor();
    const float panelWidth = 420.0f;
    const float panelHeight = 220.0f;
    const float panelX = (screen.width - panelWidth) * 0.5f;
    const float panelY = (screen.height - panelHeight) * 0.5f;

    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.rect("bg")
                .size(screen.width, screen.height)
                .color(bg)
                .build();

            ui.rect("panel")
                .x(panelX)
                .y(panelY)
                .size(panelWidth, panelHeight)
                .color(panel)
                .radius(8.0f)
                .border(1.0f, border)
                .build();

            ui.text("title")
                .x(panelX + 28.0f)
                .y(panelY + 28.0f)
                .size(panelWidth - 56.0f, 28.0f)
                .text("Custom title bar buttons")
                .fontSize(22.0f)
                .lineHeight(28.0f)
                .color(theme.text)
                .build();

            ui.text("counter")
                .x(panelX + 28.0f)
                .y(panelY + 84.0f)
                .size(panelWidth - 56.0f, 24.0f)
                .text("Left + button count: " + std::to_string(state.counter))
                .fontSize(16.0f)
                .lineHeight(24.0f)
                .color(theme.text)
                .build();

            ui.text("theme")
                .x(panelX + 28.0f)
                .y(panelY + 118.0f)
                .size(panelWidth - 56.0f, 24.0f)
                .text(std::string("Right theme button: ") + modeLabel())
                .fontSize(16.0f)
                .lineHeight(24.0f)
                .color(theme.text)
                .build();

            ui.text("pin")
                .x(panelX + 28.0f)
                .y(panelY + 152.0f)
                .size(panelWidth - 56.0f, 24.0f)
                .text(std::string("Right pin button: ") + pinLabel())
                .fontSize(16.0f)
                .lineHeight(24.0f)
                .color(theme.text)
                .build();
        })
        .build();
}

} // namespace app
