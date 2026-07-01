#include "eui_neo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace app {
namespace {

struct LogEntry {
    std::string time;
    std::string dir;
    std::string mode;
    std::string bytes;
    std::string payload;
};

struct SerialState {
    bool connected = true;
    bool autoReceive = false;
    int mode = 0;
    int tick = 0;
    int sequence = 0;
    components::LineStyle chartStyle = components::LineStyle::Linear;
    int txFrames = 0;
    int rxFrames = 0;
    int txBytes = 0;
    int rxBytes = 0;
    int textBytes = 0;
    int hexBytes = 0;
    std::array<int, 4> frameMix{{0, 0, 0, 0}};
    std::string textPayload = "Hello EUI";
    std::string hexPayload = "AA 55 01 02 03";
    std::vector<float> throughput{0.10f, 0.14f, 0.18f, 0.16f, 0.24f, 0.22f, 0.30f, 0.28f, 0.36f, 0.32f};
    std::vector<LogEntry> logs;
};

SerialState state;

constexpr eui::Color kBackground{0.930f, 0.945f, 0.940f, 1.0f};
constexpr eui::Color kSurface{0.990f, 0.992f, 0.988f, 1.0f};
constexpr eui::Color kSurfaceHover{0.900f, 0.928f, 0.920f, 1.0f};
constexpr eui::Color kSurfaceActive{0.830f, 0.875f, 0.865f, 1.0f};
constexpr eui::Color kInk{0.055f, 0.068f, 0.074f, 1.0f};
constexpr eui::Color kMuted{0.420f, 0.470f, 0.480f, 1.0f};
constexpr eui::Color kBorder{0.720f, 0.780f, 0.770f, 0.92f};
constexpr eui::Color kTeal{0.035f, 0.520f, 0.445f, 1.0f};
constexpr eui::Color kBlue{0.205f, 0.390f, 0.810f, 1.0f};
constexpr eui::Color kAmber{0.930f, 0.540f, 0.130f, 1.0f};
constexpr eui::Color kRose{0.810f, 0.235f, 0.345f, 1.0f};
constexpr eui::Color kClear{0.0f, 0.0f, 0.0f, 0.0f};

components::theme::ThemeColorTokens themeTokens() {
    return {kBackground, kTeal, kSurface, kSurfaceHover, kSurfaceActive, kInk, kBorder, false};
}

eui::Transition transition() {
    return eui::Transition::make(0.16f, eui::Ease::OutCubic);
}

eui::Transition chartTransition() {
    return eui::Transition::make(0.52f, eui::Ease::InOutCubic);
}

eui::Color alpha(eui::Color color, float value) {
    color.a = std::clamp(value, 0.0f, 1.0f);
    return color;
}

eui::Color soft(eui::Color color, float value) {
    color.a = std::clamp(value, 0.0f, 1.0f);
    return color;
}

std::string number(int value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

std::string bytesText(int value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%d B", value);
    return buffer;
}

std::string stamp() {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "T+%03d", state.tick++);
    return buffer;
}

std::string byteHex(int value) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.push_back(kDigits[(value >> 4) & 0x0F]);
    out.push_back(kDigits[value & 0x0F]);
    return out;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string normalizeHex(const std::string& input, bool& ok, int& byteCount) {
    std::string digits;
    ok = true;
    byteCount = 0;
    for (char ch : input) {
        if (std::isxdigit(static_cast<unsigned char>(ch))) {
            digits.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        } else if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == ':' || ch == '-') {
            continue;
        } else {
            ok = false;
            return {};
        }
    }

    if (digits.empty() || digits.size() % 2 != 0) {
        ok = false;
        return {};
    }

    std::string result;
    for (std::size_t i = 0; i < digits.size(); i += 2) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result.push_back(digits[i]);
        result.push_back(digits[i + 1]);
        ++byteCount;
    }
    return result;
}

int countHexBytes(const std::string& value) {
    if (value.empty()) {
        return 0;
    }
    int count = 1;
    for (char ch : value) {
        if (ch == ' ') {
            ++count;
        }
    }
    return count;
}

std::string shorten(std::string value, int limit) {
    if (static_cast<int>(value.size()) <= limit || limit < 4) {
        return value;
    }
    value.resize(static_cast<std::size_t>(limit - 3));
    return value + "...";
}

void pushTrafficValue(int byteCount) {
    const float value = std::clamp(0.08f + static_cast<float>(byteCount) / 110.0f, 0.04f, 1.0f);
    if (state.throughput.size() < 10u) {
        state.throughput.assign(10, 0.06f);
    }
    state.throughput.erase(state.throughput.begin());
    state.throughput.push_back(value);
}

void addLog(const std::string& dir, const std::string& mode, const std::string& payload, int byteCount) {
    state.logs.insert(state.logs.begin(), {stamp(), dir, mode, number(byteCount), payload});
    if (state.logs.size() > 7u) {
        state.logs.pop_back();
    }
}

void record(bool tx, bool hex, int byteCount) {
    if (tx) {
        ++state.txFrames;
        state.txBytes += byteCount;
    } else {
        ++state.rxFrames;
        state.rxBytes += byteCount;
    }

    if (hex) {
        state.hexBytes += byteCount;
    } else {
        state.textBytes += byteCount;
    }

    const int mixIndex = tx ? (hex ? 1 : 0) : (hex ? 3 : 2);
    ++state.frameMix[static_cast<std::size_t>(mixIndex)];
    pushTrafficValue(byteCount);
}

std::string mockTextPayload() {
    static constexpr std::array<const char*, 5> kSamples{{
        "ACK ready",
        "RX temperature=24.8C",
        "RX packet OK",
        "Flow stable",
        "Device heartbeat"
    }};
    return kSamples[static_cast<std::size_t>(state.sequence) % kSamples.size()];
}

std::string mockHexPayload(int hint) {
    const int a = 0xA0 | (state.sequence & 0x0F);
    const int b = hint & 0xFF;
    const int c = (state.sequence * 23 + 0x31) & 0xFF;
    const int sum = (0xAA + 0x55 + a + b + c) & 0xFF;
    return "AA 55 " + byteHex(a) + " " + byteHex(b) + " " + byteHex(c) + " " + byteHex(sum);
}

void receive(bool hex, int hint = 6) {
    if (!state.connected) {
        addLog("SYS", "INFO", "Port closed", 0);
        pushTrafficValue(0);
        return;
    }

    ++state.sequence;
    if (hex) {
        const std::string payload = mockHexPayload(hint);
        const int byteCount = countHexBytes(payload);
        addLog("RX", "HEX", payload, byteCount);
        record(false, true, byteCount);
    } else {
        const std::string payload = mockTextPayload();
        const int byteCount = static_cast<int>(payload.size());
        addLog("RX", "TEXT", payload, byteCount);
        record(false, false, byteCount);
    }
}

void send() {
    if (!state.connected) {
        addLog("SYS", "INFO", "Open port before sending", 0);
        pushTrafficValue(0);
        return;
    }

    const bool hex = state.mode == 1;
    ++state.sequence;
    if (hex) {
        bool ok = false;
        int byteCount = 0;
        const std::string payload = normalizeHex(state.hexPayload, ok, byteCount);
        if (!ok) {
            addLog("ERR", "HEX", "Use complete byte pairs, e.g. AA 55 01", 0);
            pushTrafficValue(0);
            return;
        }
        state.hexPayload = payload;
        addLog("TX", "HEX", payload, byteCount);
        record(true, true, byteCount);
        if (state.autoReceive) {
            receive(true, byteCount);
        }
    } else {
        std::string payload = trim(state.textPayload);
        if (payload.empty()) {
            payload = "Hello EUI";
        }
        state.textPayload = payload;
        const int byteCount = static_cast<int>(payload.size());
        addLog("TX", "TEXT", payload, byteCount);
        record(true, false, byteCount);
        if (state.autoReceive) {
            receive(false, byteCount);
        }
    }
}

void clearData() {
    state.txFrames = 0;
    state.rxFrames = 0;
    state.txBytes = 0;
    state.rxBytes = 0;
    state.textBytes = 0;
    state.hexBytes = 0;
    state.frameMix = {{0, 0, 0, 0}};
    state.throughput.assign(10, 0.06f);
    state.logs.clear();
}

std::vector<float> barValues() {
    int maxValue = std::max(state.txBytes, state.rxBytes);
    maxValue = std::max(maxValue, state.textBytes);
    maxValue = std::max(maxValue, state.hexBytes);
    maxValue = std::max(maxValue, 1);
    return {
        static_cast<float>(state.txBytes) / static_cast<float>(maxValue),
        static_cast<float>(state.rxBytes) / static_cast<float>(maxValue),
        static_cast<float>(state.textBytes) / static_cast<float>(maxValue),
        static_cast<float>(state.hexBytes) / static_cast<float>(maxValue)
    };
}

std::vector<float> pieValues() {
    const int total = state.frameMix[0] + state.frameMix[1] + state.frameMix[2] + state.frameMix[3];
    if (total == 0) {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }
    return {
        static_cast<float>(state.frameMix[0]),
        static_cast<float>(state.frameMix[1]),
        static_cast<float>(state.frameMix[2]),
        static_cast<float>(state.frameMix[3])
    };
}

std::string chartStyleText() {
    switch (state.chartStyle) {
    case components::LineStyle::Curve:
        return "Curve";
    case components::LineStyle::Step:
        return "Step";
    case components::LineStyle::Linear:
    default:
        return "Linear";
    }
}

void nextChartStyle() {
    switch (state.chartStyle) {
    case components::LineStyle::Linear:
        state.chartStyle = components::LineStyle::Curve;
        break;
    case components::LineStyle::Curve:
        state.chartStyle = components::LineStyle::Step;
        break;
    case components::LineStyle::Step:
        state.chartStyle = components::LineStyle::Linear;
        break;
    }
}

void label(eui::Ui& ui,
           const std::string& id,
           float x,
           float y,
           float width,
           float height,
           const std::string& value,
           float fontSize,
           eui::Color color,
           eui::HorizontalAlign align = eui::HorizontalAlign::Left) {
    ui.text(id)
        .x(x)
        .y(y)
        .size(width, height)
        .text(value)
        .fontSize(fontSize)
        .lineHeight(fontSize + 5.0f)
        .color(color)
        .horizontalAlign(align)
        .verticalAlign(eui::VerticalAlign::Center)
        .build();
}

void button(eui::Ui& ui,
            const std::string& id,
            float x,
            float y,
            float width,
            float height,
            const std::string& text,
            unsigned int icon,
            bool primary,
            std::function<void()> onClick) {
    const eui::Color base = primary ? kTeal : kSurfaceHover;
    const eui::Color hover = primary ? eui::mixColor(kTeal, {1.0f, 1.0f, 1.0f, 1.0f}, 0.12f) : eui::mixColor(kSurfaceHover, kTeal, 0.08f);
    const eui::Color pressed = primary ? eui::mixColor(kTeal, {0.0f, 0.0f, 0.0f, 1.0f}, 0.12f) : kSurfaceActive;
    const eui::Color textColor = primary ? eui::Color{0.98f, 1.0f, 0.99f, 1.0f} : kInk;

    ui.stack(id + ".wrap")
        .x(x)
        .y(y)
        .size(width, height)
        .content([&] {
            components::button(ui, id)
                .size(width, height)
                .icon(icon)
                .iconSize(13.0f)
                .fontSize(13.0f)
                .text(text)
                .colors(base, hover, pressed)
                .textColor(textColor)
                .iconColor(primary ? textColor : kTeal)
                .radius(8.0f)
                .border(1.0f, primary ? alpha(kTeal, 0.58f) : alpha(kBorder, 0.70f))
                .shadow(0.0f, 0.0f, 0.0f, kClear)
                .transition(transition())
                .onClick(std::move(onClick))
                .build();
        })
        .build();
}

void panel(eui::Ui& ui, const std::string& id, float x, float y, float width, float height) {
    ui.rect(id)
        .x(x)
        .y(y)
        .size(width, height)
        .color(kSurface)
        .radius(10.0f)
        .border(1.0f, alpha(kBorder, 0.78f))
        .build();
}

void composeHeader(eui::Ui& ui, float x, float y, float width) {
    const float statusW = 118.0f;
    const float statusX = x + width - 394.0f;
    label(ui, "header.title", x, y, 170.0f, 34.0f, "Serial Tool", 24.0f, kInk);
    label(ui, "header.port", x + 176.0f, y, std::max(120.0f, statusX - x - 192.0f), 34.0f, "COM3  115200  8N1", 14.0f, kMuted);

    ui.rect("header.status.bg")
        .x(statusX)
        .y(y)
        .size(statusW, 34.0f)
        .color(state.connected ? soft(kTeal, 0.12f) : soft(kRose, 0.12f))
        .radius(8.0f)
        .border(1.0f, state.connected ? alpha(kTeal, 0.32f) : alpha(kRose, 0.32f))
        .build();
    ui.text("header.status.text")
        .x(statusX)
        .y(y)
        .size(statusW, 34.0f)
        .text(state.connected ? "Connected" : "Closed")
        .fontSize(13.0f)
        .lineHeight(13.0f)
        .color(state.connected ? kTeal : kRose)
        .horizontalAlign(eui::HorizontalAlign::Center)
        .verticalAlign(eui::VerticalAlign::Center)
        .build();

    button(ui, "header.connect", x + width - 266.0f, y, 126.0f, 34.0f,
           state.connected ? "Close Port" : "Open Port",
           state.connected ? 0xF00D : 0xF011,
           false,
           [] {
               state.connected = !state.connected;
               addLog("SYS", "INFO", state.connected ? "COM3 opened" : "COM3 closed", 0);
           });
    button(ui, "header.clear", x + width - 130.0f, y, 130.0f, 34.0f, "Clear Data", 0xF12D, false, [] {
        clearData();
    });
}

void composeMetricBar(eui::Ui& ui, float x, float y, float width) {
    const float h = 58.0f;
    const float cellW = width / 6.0f;
    panel(ui, "metrics.bg", x, y, width, h);

    auto cell = [&](const std::string& id, int index, const std::string& name, const std::string& value, eui::Color color) {
        const float cx = x + static_cast<float>(index) * cellW;
        if (index > 0) {
            ui.rect(id + ".line")
                .x(cx)
                .y(y + 11.0f)
                .size(1.0f, h - 22.0f)
                .color(alpha(kBorder, 0.52f))
                .build();
        }
        ui.rect(id + ".dot")
            .x(cx + 14.0f)
            .y(y + 16.0f)
            .size(7.0f, 7.0f)
            .color(color)
            .radius(3.5f)
            .build();
        label(ui, id + ".name", cx + 28.0f, y + 8.0f, cellW - 36.0f, 20.0f, name, 12.0f, kMuted);
        label(ui, id + ".value", cx + 28.0f, y + 30.0f, cellW - 36.0f, 22.0f, value, 18.0f, kInk);
    };

    cell("metric.tx.frames", 0, "TX Frames", number(state.txFrames), kTeal);
    cell("metric.rx.frames", 1, "RX Frames", number(state.rxFrames), kBlue);
    cell("metric.tx.bytes", 2, "TX Bytes", bytesText(state.txBytes), kTeal);
    cell("metric.rx.bytes", 3, "RX Bytes", bytesText(state.rxBytes), kBlue);
    cell("metric.text.bytes", 4, "Text Bytes", number(state.textBytes), kAmber);
    cell("metric.hex.bytes", 5, "HEX Bytes", number(state.hexBytes), kRose);
}

void composeCharts(eui::Ui& ui, float x, float y, float width) {
    const float gap = 16.0f;
    const float h = 346.0f;
    const float sideW = std::clamp(width * 0.145f, 160.0f, 190.0f);
    const float lineW = std::max(320.0f, width - sideW * 2.0f - gap * 2.0f);
    const std::vector<eui::Color> colors{kTeal, kBlue, kAmber, kRose};

    ui.stack("charts.line.wrap")
        .x(x)
        .y(y)
        .size(lineW, h)
        .content([&] {
            components::lineChart(ui, "charts.line")
                .theme(themeTokens())
                .size(lineW, h)
                .title("Throughput")
                .values(state.throughput)
                .labels({"-9", "-8", "-7", "-6", "-5", "-4", "-3", "-2", "-1", "Now"})
                .style(state.chartStyle)
                .transition(chartTransition())
                .build();

            button(ui, "charts.line.style", lineW - 116.0f, 16.0f, 96.0f, 30.0f,
                   chartStyleText(), 0xF1FC, false, [] {
                       nextChartStyle();
                   });
        })
        .build();

    ui.stack("charts.bar.wrap")
        .x(x + lineW + gap)
        .y(y)
        .size(sideW, h)
        .content([&] {
            components::barChart(ui, "charts.bar")
                .theme(themeTokens())
                .size(sideW, h)
                .title("Bytes")
                .values(barValues())
                .labels({"TX", "RX", "TXT", "HEX"})
                .colors(colors)
                .transition(chartTransition())
                .build();
        })
        .build();

    ui.stack("charts.pie.wrap")
        .x(x + lineW + gap + sideW + gap)
        .y(y)
        .size(sideW, h)
        .content([&] {
            components::pieChart(ui, "charts.pie")
                .theme(themeTokens())
                .size(sideW, h)
                .title("Mix")
                .values(pieValues())
                .labels({"TX Text", "TX HEX", "RX Text", "RX HEX"})
                .colors(colors)
                .transition(chartTransition())
                .build();
        })
        .build();
}

void composeTransmit(eui::Ui& ui, float x, float y, float width, float height) {
    panel(ui, "tx.bg", x, y, width, height);
    label(ui, "tx.title", x + 14.0f, y + 10.0f, 86.0f, 28.0f, "Simulator", 15.0f, kInk);

    ui.stack("tx.mode.wrap")
        .x(x + 104.0f)
        .y(y + 9.0f)
        .size(156.0f, 30.0f)
        .content([&] {
            components::segmented(ui, "tx.mode")
                .theme(themeTokens())
                .size(156.0f, 30.0f)
                .items({"Text", "HEX"})
                .selected(state.mode)
                .fontSize(12.0f)
                .onChange([](int value) {
                    state.mode = value;
                })
                .build();
        })
        .build();

    ui.stack("tx.auto.wrap")
        .x(x + width - 118.0f)
        .y(y + 10.0f)
        .size(112.0f, 28.0f)
        .content([&] {
            components::toggleSwitch(ui, "tx.auto")
                .theme(themeTokens())
                .size(112.0f, 28.0f)
                .trackSize(36.0f, 20.0f)
                .fontSize(11.0f)
                .text("Auto RX")
                .checked(state.autoReceive)
                .onChange([](bool value) {
                    state.autoReceive = value;
                })
                .build();
        })
        .build();

    ui.stack("tx.input.wrap")
        .x(x + 14.0f)
        .y(y + 52.0f)
        .size(width - 28.0f, 36.0f)
        .content([&] {
            components::input(ui, "tx.input")
                .theme(themeTokens())
                .size(width - 28.0f, 36.0f)
                .fontSize(14.0f)
                .placeholder(state.mode == 0 ? "Text payload" : "AA 55 01 02")
                .value(state.mode == 0 ? state.textPayload : state.hexPayload)
                .onChange([](const std::string& value) {
                    if (state.mode == 0) {
                        state.textPayload = value;
                    } else {
                        state.hexPayload = value;
                    }
                })
                .onEnter([] {
                    send();
                })
                .build();
        })
        .build();

    const float gap = 10.0f;
    const float buttonW = (width - 28.0f - gap * 2.0f) / 3.0f;
    const float by = y + height - 42.0f;
    button(ui, "tx.send", x + 14.0f, by, buttonW, 32.0f, "Send", 0xF1D8, true, [] { send(); });
    button(ui, "tx.rx", x + 14.0f + buttonW + gap, by, buttonW, 32.0f, "Mock RX", 0xF2F1, false, [] {
        receive(state.mode == 1, 8);
    });
    button(ui, "tx.clear", x + 14.0f + (buttonW + gap) * 2.0f, by, buttonW, 32.0f, "Clear", 0xF12D, false, [] {
        clearData();
    });
}

eui::Color dirColor(const std::string& dir) {
    if (dir == "TX") {
        return kTeal;
    }
    if (dir == "RX") {
        return kBlue;
    }
    if (dir == "ERR") {
        return kRose;
    }
    return kMuted;
}

void composeLog(eui::Ui& ui, float x, float y, float width, float height) {
    panel(ui, "log.bg", x, y, width, height);
    label(ui, "log.title", x + 14.0f, y + 10.0f, width - 28.0f, 28.0f, "Traffic Data", 15.0f, kInk);

    const float tx = x + 14.0f;
    const float ty = y + 42.0f;
    const float tw = width - 28.0f;
    const float hh = 24.0f;
    const float rowH = 22.0f;
    const float timeW = 68.0f;
    const float dirW = 52.0f;
    const float modeW = 62.0f;
    const float bytesW = 58.0f;
    const float payloadW = std::max(80.0f, tw - timeW - dirW - modeW - bytesW);

    ui.rect("log.header.bg")
        .x(tx)
        .y(ty)
        .size(tw, hh)
        .color(kSurfaceHover)
        .radius(7.0f)
        .border(1.0f, alpha(kBorder, 0.58f))
        .build();
    label(ui, "log.h.time", tx + 10.0f, ty, timeW - 10.0f, hh, "Time", 12.0f, kMuted);
    label(ui, "log.h.dir", tx + timeW, ty, dirW, hh, "Dir", 12.0f, kMuted);
    label(ui, "log.h.mode", tx + timeW + dirW, ty, modeW, hh, "Mode", 12.0f, kMuted);
    label(ui, "log.h.bytes", tx + timeW + dirW + modeW, ty, bytesW, hh, "Bytes", 12.0f, kMuted);
    label(ui, "log.h.payload", tx + timeW + dirW + modeW + bytesW, ty, payloadW - 8.0f, hh, "Payload", 12.0f, kMuted);

    if (state.logs.empty()) {
        label(ui, "log.empty", tx, ty + 54.0f, tw, 24.0f, "No traffic yet.", 13.0f, kMuted, eui::HorizontalAlign::Center);
        return;
    }

    const int rows = std::min<int>(5, static_cast<int>(state.logs.size()));
    const int payloadChars = std::max(10, static_cast<int>(payloadW / 8.0f));
    for (int i = 0; i < rows; ++i) {
        const LogEntry& item = state.logs[static_cast<std::size_t>(i)];
        const float ry = ty + hh + 6.0f + static_cast<float>(i) * (rowH + 4.0f);
        ui.rect("log.row." + std::to_string(i))
            .x(tx)
            .y(ry)
            .size(tw, rowH)
            .color(i % 2 == 0 ? alpha(kSurfaceHover, 0.42f) : alpha(kSurfaceActive, 0.25f))
            .radius(7.0f)
            .build();
        label(ui, "log.time." + std::to_string(i), tx + 10.0f, ry, timeW - 10.0f, rowH, item.time, 12.0f, kMuted);
        label(ui, "log.dir." + std::to_string(i), tx + timeW, ry, dirW, rowH, item.dir, 12.0f, dirColor(item.dir));
        label(ui, "log.mode." + std::to_string(i), tx + timeW + dirW, ry, modeW, rowH, item.mode, 12.0f, kMuted);
        label(ui, "log.bytes." + std::to_string(i), tx + timeW + dirW + modeW, ry, bytesW, rowH, item.bytes, 12.0f, kMuted);
        label(ui, "log.payload." + std::to_string(i), tx + timeW + dirW + modeW + bytesW, ry, payloadW - 8.0f, rowH, shorten(item.payload, payloadChars), 12.0f, kInk);
    }
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("Serial Tool")
        .pageId("serial_tool")
        .clearColor(kBackground)
        .windowSize(1440, 1080)
        .fps(90.0)
        .tray(true);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    const float margin = std::clamp(screen.width * 0.025f, 18.0f, 30.0f);
    const float contentW = std::max(720.0f, screen.width - margin * 2.0f);
    const float x = (screen.width - contentW) * 0.5f;
    const float headerY = 16.0f;
    const float metricsY = 62.0f;
    const float chartsY = 136.0f;
    const float bottomY = 500.0f;
    const float bottomH = 218.0f;
    const float txW = std::clamp(contentW * 0.34f, 430.0f, 470.0f);
    const float gap = 16.0f;

    ui.stack("root")
        .size(screen.width, screen.height)
        .content([&] {
            ui.rect("background")
                .size(screen.width, screen.height)
                .color(kBackground)
                .build();

            composeHeader(ui, x, headerY, contentW);
            composeMetricBar(ui, x, metricsY, contentW);
            composeCharts(ui, x, chartsY, contentW);
            composeTransmit(ui, x, bottomY, txW, bottomH);
            composeLog(ui, x + txW + gap, bottomY, contentW - txW - gap, bottomH);

            ui.stack("auto.receive.timer")
                .size(0.0f, 0.0f)
                .onTimer(1.35f, [] {
                    if (state.connected && state.autoReceive) {
                        receive((state.sequence % 2) == 0, 6);
                    }
                })
                .build();

        })
        .build();
}

} // namespace app
