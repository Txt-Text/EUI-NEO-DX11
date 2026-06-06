#include "eui_neo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>

namespace app {
namespace {

struct Message {
    std::string author;
    std::string text;
    std::string time;
    bool outgoing = false;
};

struct Chat {
    std::string name;
    std::string status;
    std::string handle;
    bool online = false;
    bool pinned = false;
    bool muted = false;
    bool archived = false;
    int unread = 0;
    std::vector<Message> messages;
};

std::vector<Chat> chats{
    {"EUI Core", "3 members online", "@eui_core", true, true, false, false, 2,
     {{"Mina", "The panel API looks stable now.", "09:41", false},
      {"You", "Nice. I am wiring the chat demo with real state.", "09:42", true},
      {"Kai", "Please keep the composer keyboard friendly.", "09:44", false}}},
    {"Design Review", "last seen recently", "@design_review", false, false, false, false, 0,
     {{"Anya", "Can we make the sidebar behave like Telegram?", "Yesterday", false},
      {"You", "Yes. Search, unread filter and selection are all live.", "Yesterday", true}}},
    {"Build Bots", "online", "@build_bots", true, false, true, false, 5,
     {{"CI", "opengl-glfw-release completed successfully.", "08:15", false},
      {"CI", "Probe suite is ready for review.", "08:17", false}}},
    {"Archived Notes", "archived", "@notes", false, false, false, true, 0,
     {{"You", "Ideas for the next dashboard pass.", "Mon", true}}},
};

int selectedChat = 0;
int filterMode = 0;
int accentIndex = 0;
bool darkMode = false;
bool showArchived = false;
bool settingsOpen = false;
bool menuOpen = false;
int menuKind = 0;
int menuTarget = -1;
float menuX = 0.0f;
float menuY = 0.0f;
float chatListOffset = 0.0f;
float messageOffset = 0.0f;
std::string chatSearch;
std::string draftText;
std::string newChatName;

eui::Color rgba(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

eui::Color rgb(int r, int g, int b, float a = 1.0f) {
    return {static_cast<float>(r) / 255.0f,
            static_cast<float>(g) / 255.0f,
            static_cast<float>(b) / 255.0f,
            a};
}

components::theme::ThemeColorTokens theme() {
    auto tokens = darkMode ? components::theme::DarkThemeColors() : components::theme::LightThemeColors();
    const std::array<eui::Color, 4> accents{{rgb(51, 144, 236), rgb(10, 151, 138), rgb(126, 87, 194), rgb(224, 111, 38)}};
    tokens.primary = accents[static_cast<std::size_t>(accentIndex)];
    return tokens;
}

eui::Color pageBg() { return darkMode ? rgb(15, 23, 33) : rgb(218, 226, 235); }
eui::Color panelBg() { return darkMode ? rgb(33, 43, 54) : rgb(255, 255, 255); }
eui::Color sidebarBg() { return darkMode ? rgb(26, 36, 48) : rgb(244, 248, 251); }
eui::Color chatBg() { return darkMode ? rgb(12, 22, 33) : rgb(222, 235, 247); }
eui::Color incomingBg() { return darkMode ? rgb(33, 43, 54) : rgb(255, 255, 255); }
eui::Color softBg() { return darkMode ? rgb(36, 46, 60) : rgb(235, 242, 249); }
eui::Color textMain() { return darkMode ? rgb(241, 245, 249) : rgb(18, 25, 38); }
eui::Color textMuted() { return darkMode ? rgb(151, 164, 184) : rgb(95, 111, 132); }
eui::Color borderColor() { return darkMode ? rgb(54, 66, 84) : rgb(215, 224, 236); }

std::string lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool contains(const std::string& text, const std::string& needle) {
    return needle.empty() || lower(text).find(lower(needle)) != std::string::npos;
}

bool chatVisible(const Chat& chat) {
    if (!showArchived && chat.archived) {
        return false;
    }
    if (filterMode == 1 && chat.unread == 0) {
        return false;
    }
    if (filterMode == 2 && !chat.pinned) {
        return false;
    }
    return contains(chat.name, chatSearch) || contains(chat.handle, chatSearch) || contains(chat.status, chatSearch);
}

void selectChat(int index) {
    selectedChat = std::clamp(index, 0, std::max(0, static_cast<int>(chats.size()) - 1));
    chats[static_cast<std::size_t>(selectedChat)].unread = 0;
}

void sendMessage() {
    if (draftText.empty() || chats.empty()) {
        return;
    }
    Chat& chat = chats[static_cast<std::size_t>(selectedChat)];
    chat.messages.push_back({"You", draftText, "now", true});
    chat.messages.push_back({chat.name, "Got it. I will check this and reply with details.", "now", false});
    draftText.clear();
    messageOffset = 100000.0f;
}

void createChat() {
    const std::string name = newChatName.empty() ? "New Chat" : newChatName;
    chats.push_back({name, "new conversation", "@" + lower(name), true, false, false, false, 0, {{"System", "Conversation created.", "now", false}}});
    newChatName.clear();
    selectChat(static_cast<int>(chats.size()) - 1);
}

void openMenu(int kind, int target, float x, float y) {
    menuOpen = true;
    menuKind = kind;
    menuTarget = target;
    menuX = x;
    menuY = y;
}

void applyMenuAction(int action) {
    if (menuKind == 0 && menuTarget >= 0 && menuTarget < static_cast<int>(chats.size())) {
        Chat& chat = chats[static_cast<std::size_t>(menuTarget)];
        if (action == 0) {
            selectChat(menuTarget);
        } else if (action == 1) {
            chat.pinned = !chat.pinned;
        } else if (action == 2) {
            chat.muted = !chat.muted;
        } else if (action == 3) {
            chat.archived = !chat.archived;
        }
    } else if (menuKind == 1 && menuTarget >= 0) {
        Chat& chat = chats[static_cast<std::size_t>(selectedChat)];
        if (menuTarget < static_cast<int>(chat.messages.size())) {
            if (action == 0) {
                draftText = chat.messages[static_cast<std::size_t>(menuTarget)].text;
            } else if (action == 1) {
                chat.messages.erase(chat.messages.begin() + menuTarget);
            }
        }
    }
    menuOpen = false;
}

void label(eui::Ui& ui, const std::string& id, float w, float h, const std::string& text, float size, eui::Color color, int weight = 400, eui::HorizontalAlign align = eui::HorizontalAlign::Left) {
    components::text(ui, id)
        .size(w, h)
        .text(text)
        .fontSize(size)
        .fontWeight(weight)
        .lineHeight(size + 3.0f)
        .color(color)
        .horizontalAlign(align)
        .verticalAlign(eui::VerticalAlign::Center)
        .build();
}

void panel(eui::Ui& ui, const std::string& id, float w, float h, eui::Color fill) {
    components::panel(ui, id)
        .size(w, h)
        .radius(12.0f)
        .color(fill)
        .border(1.0f, borderColor())
        .shadow(14.0f, 0.0f, 6.0f, darkMode ? rgba(0, 0, 0, 0.24f) : rgba(0.12f, 0.16f, 0.24f, 0.08f))
        .build();
}

void avatar(eui::Ui& ui, const std::string& id, float size, const std::string& name, bool online) {
    ui.stack(id).size(size, size).content([&] {
        ui.rect(id + ".bg").size(size, size).color(theme().primary).radius(size * 0.5f).build();
        label(ui, id + ".letter", size, size, name.substr(0, 1), size * 0.42f, rgb(255, 255, 255), 800, eui::HorizontalAlign::Center);
        if (online) {
            ui.rect(id + ".online")
                .x(size - 14.0f)
                .y(size - 14.0f)
                .size(12.0f, 12.0f)
                .color(rgb(45, 212, 105))
                .radius(6.0f)
                .border(2.0f, panelBg())
                .build();
        }
    }).build();
}

void chatRow(eui::Ui& ui, int index, float w) {
    const Chat& chat = chats[static_cast<std::size_t>(index)];
    const bool active = index == selectedChat;
    ui.stack("chat.row." + std::to_string(index)).size(w, 74.0f).content([&] {
        ui.rect("chat.row.bg." + std::to_string(index))
            .size(w, 74.0f)
            .color(active ? components::theme::withAlpha(theme().primary, darkMode ? 0.28f : 0.14f) : rgba(0, 0, 0, 0))
            .radius(10.0f)
            .build();
        ui.row("chat.row.content." + std::to_string(index))
            .size(w, 74.0f)
            .padding(10.0f)
            .gap(12.0f)
            .alignItems(eui::Align::CENTER)
            .content([&] {
                avatar(ui, "chat.avatar." + std::to_string(index), 46.0f, chat.name, chat.online);
                ui.column("chat.copy." + std::to_string(index)).size(w - 112.0f, 48.0f).gap(4.0f).content([&] {
                    label(ui, "chat.name." + std::to_string(index), w - 112.0f, 22.0f, (chat.pinned ? "* " : "") + chat.name, 15.0f, textMain(), 750);
                    label(ui, "chat.preview." + std::to_string(index), w - 112.0f, 20.0f, chat.messages.empty() ? chat.status : chat.messages.back().text, 13.0f, textMuted(), 500);
                }).build();
                if (chat.unread > 0) {
                    ui.stack("chat.unread.badge." + std::to_string(index)).size(28.0f, 24.0f).content([&] {
                        ui.rect("chat.unread.bg." + std::to_string(index)).size(28.0f, 24.0f).color(theme().primary).radius(12.0f).build();
                        label(ui, "chat.unread." + std::to_string(index), 28.0f, 24.0f, std::to_string(chat.unread), 12.0f, rgb(255, 255, 255), 800, eui::HorizontalAlign::Center);
                    }).build();
                }
            })
            .build();
        components::mouseArea(ui, "chat.hit." + std::to_string(index))
            .size(w, 74.0f)
            .radius(10.0f)
            .onClick([index] { selectChat(index); })
            .onContextMenu([index](const components::MouseEvent& event) {
                openMenu(0, index, event.globalX, event.globalY);
            })
            .build();
    }).build();
}

void sidebar(eui::Ui& ui, float w, float h) {
    ui.stack("sidebar").size(w, h).content([&] {
        panel(ui, "sidebar.panel", w, h, sidebarBg());
        ui.column("sidebar.content").size(w, h).padding(16.0f).gap(12.0f).content([&] {
            ui.row("sidebar.top").size(w - 32.0f, 42.0f).gap(10.0f).alignItems(eui::Align::CENTER).content([&] {
                label(ui, "app.title", w - 146.0f, 32.0f, "Telegram", 22.0f, textMain(), 850);
                components::button(ui, "new.chat").theme(theme(), true).size(104.0f, 36.0f).text("New").fontSize(13.0f).onClick([] { createChat(); }).build();
            }).build();
            components::input(ui, "chat.search")
                .theme(theme())
                .size(w - 32.0f, 40.0f)
                .value(chatSearch)
                .placeholder("Search")
                .onChange([](const std::string& value) { chatSearch = value; })
                .build();
            components::segmented(ui, "chat.filter")
                .theme(theme())
                .size(w - 32.0f, 36.0f)
                .items({"All", "Unread", "Pinned"})
                .selected(filterMode)
                .fontSize(12.0f)
                .onChange([](int index) { filterMode = index; })
                .build();
            components::toggleSwitch(ui, "chat.archived")
                .theme(theme())
                .size(w - 32.0f, 30.0f)
                .label("Show archived")
                .checked(showArchived)
                .onChange([](bool value) { showArchived = value; })
                .build();
            components::input(ui, "chat.new.name")
                .theme(theme())
                .size(w - 32.0f, 38.0f)
                .value(newChatName)
                .placeholder("New chat name")
                .onChange([](const std::string& value) { newChatName = value; })
                .onEnter([] { createChat(); })
                .build();
            components::scrollView(ui, "chat.list")
                .theme(theme())
                .size(w - 32.0f, h - 230.0f)
                .offset(chatListOffset)
                .gap(6.0f)
                .onChange([](float value) { chatListOffset = value; })
                .content([](eui::Ui& contentUi, float contentW, float) {
                    int count = 0;
                    for (int i = 0; i < static_cast<int>(chats.size()); ++i) {
                        if (!chatVisible(chats[static_cast<std::size_t>(i)])) {
                            continue;
                        }
                        chatRow(contentUi, i, contentW);
                        ++count;
                    }
                    if (count == 0) {
                        label(contentUi, "chat.empty", contentW, 44.0f, "No chats found", 14.0f, textMuted(), 600, eui::HorizontalAlign::Center);
                    }
                })
                .build();
        }).build();
    }).build();
}

void messageBubble(eui::Ui& ui, int chatIndex, int messageIndex, float w) {
    const Message& message = chats[static_cast<std::size_t>(chatIndex)].messages[static_cast<std::size_t>(messageIndex)];
    const float bubbleW = std::min(w * 0.72f, std::max(220.0f, static_cast<float>(message.text.size()) * 7.0f + 64.0f));
    const float rowH = message.text.size() > 58 ? 92.0f : 68.0f;
    ui.row("message.row." + std::to_string(messageIndex))
        .size(w, rowH)
        .justifyContent(message.outgoing ? eui::Align::END : eui::Align::START)
        .content([&] {
            ui.stack("message.bubble." + std::to_string(messageIndex)).size(bubbleW, rowH - 8.0f).content([&] {
                ui.rect("message.bg." + std::to_string(messageIndex))
                    .size(bubbleW, rowH - 8.0f)
                    .color(message.outgoing ? theme().primary : incomingBg())
                    .radius(14.0f)
                    .border(1.0f, message.outgoing ? theme().primary : borderColor())
                    .build();
                ui.column("message.copy." + std::to_string(messageIndex))
                    .size(bubbleW, rowH - 8.0f)
                    .padding(12.0f)
                    .gap(4.0f)
                    .content([&] {
                        label(ui, "message.text." + std::to_string(messageIndex), bubbleW - 24.0f, rowH - 38.0f, message.text, 14.0f, message.outgoing ? rgb(255, 255, 255) : textMain(), 500);
                        label(ui, "message.time." + std::to_string(messageIndex), bubbleW - 24.0f, 16.0f, message.time, 11.0f, message.outgoing ? rgb(226, 238, 255) : textMuted(), 500, eui::HorizontalAlign::Right);
                    }).build();
                components::mouseArea(ui, "message.menu.hit." + std::to_string(messageIndex))
                    .size(bubbleW, rowH - 8.0f)
                    .radius(14.0f)
                    .onContextMenu([messageIndex](const components::MouseEvent& event) {
                        openMenu(1, messageIndex, event.globalX, event.globalY);
                    })
                    .build();
            }).build();
        })
        .build();
}

void chatHeader(eui::Ui& ui, float w) {
    Chat& chat = chats[static_cast<std::size_t>(selectedChat)];
    ui.stack("chat.header").size(w, 72.0f).content([&] {
        panel(ui, "chat.header.panel", w, 72.0f, panelBg());
        ui.row("chat.header.content").size(w, 72.0f).padding(14.0f).gap(12.0f).alignItems(eui::Align::CENTER).content([&] {
            avatar(ui, "header.avatar", 44.0f, chat.name, chat.online);
            ui.column("header.copy").size(w - 420.0f, 44.0f).gap(2.0f).content([&] {
                label(ui, "header.name", w - 420.0f, 24.0f, chat.name, 18.0f, textMain(), 800);
                label(ui, "header.status", w - 420.0f, 18.0f, chat.muted ? "muted - " + chat.status : chat.status, 12.0f, textMuted(), 500);
            }).build();
            components::button(ui, "chat.pin").theme(theme(), false).size(72.0f, 36.0f).text(chat.pinned ? "Unpin" : "Pin").fontSize(12.0f).onClick([] { chats[static_cast<std::size_t>(selectedChat)].pinned = !chats[static_cast<std::size_t>(selectedChat)].pinned; }).build();
            components::button(ui, "chat.mute").theme(theme(), false).size(76.0f, 36.0f).text(chat.muted ? "Unmute" : "Mute").fontSize(12.0f).onClick([] { chats[static_cast<std::size_t>(selectedChat)].muted = !chats[static_cast<std::size_t>(selectedChat)].muted; }).build();
            components::button(ui, "chat.archive").theme(theme(), false).size(86.0f, 36.0f).text(chat.archived ? "Restore" : "Archive").fontSize(12.0f).onClick([] { chats[static_cast<std::size_t>(selectedChat)].archived = !chats[static_cast<std::size_t>(selectedChat)].archived; }).build();
            components::button(ui, "chat.settings").theme(theme(), true).size(42.0f, 36.0f).text("...").fontSize(14.0f).onClick([] { settingsOpen = !settingsOpen; }).build();
        }).build();
    }).build();
}

void chatPane(eui::Ui& ui, float w, float h) {
    selectedChat = std::clamp(selectedChat, 0, std::max(0, static_cast<int>(chats.size()) - 1));
    Chat& chat = chats[static_cast<std::size_t>(selectedChat)];
    ui.column("chat.pane").size(w, h).gap(12.0f).content([&] {
        chatHeader(ui, w);
        ui.stack("messages.card").size(w, h - 148.0f).content([&] {
            panel(ui, "messages.panel", w, h - 148.0f, chatBg());
            ui.stack("messages.scroll.wrap")
                .x(14.0f)
                .y(14.0f)
                .size(w - 28.0f, h - 176.0f)
                .content([&] {
                    components::scrollView(ui, "messages.scroll")
                        .theme(theme())
                        .size(w - 28.0f, h - 176.0f)
                        .offset(messageOffset)
                        .gap(8.0f)
                        .onChange([](float value) { messageOffset = value; })
                        .content([&](eui::Ui& contentUi, float contentW, float) {
                            const float bubbleAreaW = std::max(1.0f, contentW - 28.0f);
                            for (int i = 0; i < static_cast<int>(chat.messages.size()); ++i) {
                                contentUi.stack("message.inset." + std::to_string(i))
                                    .x(14.0f)
                                    .size(bubbleAreaW, chat.messages[static_cast<std::size_t>(i)].text.size() > 58 ? 92.0f : 68.0f)
                                    .content([&] {
                                        messageBubble(contentUi, selectedChat, i, bubbleAreaW);
                                    })
                                    .build();
                            }
                        })
                        .build();
                })
                .build();
        }).build();
        ui.row("composer").size(w, 64.0f).gap(12.0f).alignItems(eui::Align::CENTER).content([&] {
            components::input(ui, "message.input")
                .theme(theme())
                .size(w - 128.0f, 46.0f)
                .value(draftText)
                .placeholder("Message")
                .onChange([](const std::string& value) { draftText = value; })
                .onEnter([] { sendMessage(); })
                .build();
            components::button(ui, "message.send")
                .theme(theme(), true)
                .size(116.0f, 46.0f)
                .text("Send")
                .fontSize(14.0f)
                .onClick([] { sendMessage(); })
                .build();
        }).build();
    }).build();
}

void settingsPane(eui::Ui& ui, float w, float h) {
    ui.stack("settings").size(w, h).content([&] {
        panel(ui, "settings.panel", w, h, panelBg());
        ui.column("settings.content").size(w, h).padding(24.0f).gap(18.0f).content([&] {
            label(ui, "settings.title", w - 48.0f, 34.0f, "Chat demo settings", 26.0f, textMain(), 850);
            components::toggleSwitch(ui, "settings.dark").theme(theme()).size(320.0f, 34.0f).label("Night mode").checked(darkMode).onChange([](bool value) { darkMode = value; }).build();
            components::segmented(ui, "settings.accent")
                .theme(theme())
                .size(w - 48.0f, 40.0f)
                .items({"Blue", "Teal", "Purple", "Orange"})
                .selected(accentIndex)
                .fontSize(12.0f)
                .onChange([](int index) { accentIndex = index; })
                .build();
            components::button(ui, "settings.mark.read").theme(theme(), false).size(220.0f, 42.0f).text("Mark all read").onClick([] {
                for (Chat& chat : chats) {
                    chat.unread = 0;
                }
            }).build();
        }).build();
    }).build();
}

void overlaySettings(eui::Ui& ui, float screenW, float screenH) {
    if (!settingsOpen) {
        return;
    }
    ui.stack("settings.overlay")
        .size(screenW, screenH)
        .zIndex(900)
        .content([&] {
            components::mouseArea(ui, "settings.dismiss")
                .size(screenW, screenH)
                .color(rgba(0, 0, 0, 0.08f))
                .onClick([] { settingsOpen = false; })
                .build();
            ui.stack("settings.popover")
                .x(std::max(18.0f, screenW - 378.0f))
                .y(86.0f)
                .size(340.0f, 238.0f)
                .content([&] {
                    panel(ui, "settings.popover.panel", 340.0f, 238.0f, panelBg());
                    ui.column("settings.popover.content")
                        .size(340.0f, 238.0f)
                        .padding(20.0f)
                        .gap(16.0f)
                        .content([&] {
                            label(ui, "settings.popover.title", 300.0f, 28.0f, "Telegram Settings", 22.0f, textMain(), 850);
                            components::toggleSwitch(ui, "settings.dark").theme(theme()).size(300.0f, 34.0f).label("Night mode").checked(darkMode).onChange([](bool value) { darkMode = value; }).build();
                            components::segmented(ui, "settings.accent").theme(theme()).size(300.0f, 38.0f).items({"Blue", "Teal", "Purple", "Orange"}).selected(accentIndex).fontSize(11.0f).onChange([](int index) { accentIndex = index; }).build();
                            components::button(ui, "settings.mark.read").theme(theme(), false).size(180.0f, 38.0f).text("Mark all read").fontSize(13.0f).onClick([] {
                                for (Chat& chat : chats) {
                                    chat.unread = 0;
                                }
                            }).build();
                        })
                        .build();
                })
                .build();
        })
        .build();
}

void overlayMenu(eui::Ui& ui, float screenW, float screenH) {
    const std::vector<std::string> items = menuKind == 0
        ? std::vector<std::string>{"Open chat", "Pin / unpin", "Mute / unmute", "Archive / restore"}
        : std::vector<std::string>{"Quote message", "Delete message"};
    components::contextMenu(ui, "context.menu")
        .theme(theme())
        .open(menuOpen)
        .screen(screenW, screenH)
        .position(menuX, menuY)
        .size(menuKind == 0 ? 190.0f : 170.0f, 36.0f)
        .items(items)
        .onSelect([](int index) { applyMenuAction(index); })
        .onDismiss([] { menuOpen = false; })
        .build();
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Telegram Chat Demo")
        .pageId("telegram-chat")
        .clearColor(rgb(229, 235, 243))
        .windowSize(1280, 820)
        .fps(60.0)
        .showDebugStatsInTitle(false);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    const float pad = 18.0f;
    const float sidebarW = 380.0f;
    const float h = screen.height - pad * 2.0f;
    const float chatW = std::max(560.0f, screen.width - sidebarW - pad * 3.0f);
    ui.stack("root").size(screen.width, screen.height).content([&] {
        ui.rect("bg").size(screen.width, screen.height).color(pageBg()).build();
        ui.row("layout").x(pad).y(pad).size(screen.width - pad * 2.0f, h).gap(18.0f).content([&] {
            sidebar(ui, sidebarW, h);
            chatPane(ui, chatW, h);
        }).build();
        overlaySettings(ui, screen.width, screen.height);
        overlayMenu(ui, screen.width, screen.height);
    }).build();
}

} // namespace app
