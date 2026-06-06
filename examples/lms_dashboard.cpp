#include "eui_neo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

namespace app {
namespace {

struct Task {
    std::string title;
    std::string course;
    std::string due;
    float progress;
    bool done;
};

int activeNav = 0;
int activeTab = 0;
int rangeIndex = 1;
int selectedTask = 0;
bool focusMode = true;
bool reminders = true;
bool darkMode = false;
int accentIndex = 0;
float weeklyGoal = 0.68f;
std::string searchQuery;

std::vector<Task> tasks{
    {"Design systems review", "Interface Design", "Today 14:00", 0.82f, false},
    {"Quiz preparation", "Statistics", "Tomorrow 09:30", 0.54f, false},
    {"Reading notes", "Product Strategy", "Friday", 1.0f, true},
    {"Team critique", "Visual Communication", "Next Monday", 0.36f, false},
};

eui::Color color(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

eui::Color rgb(int r, int g, int b, float a = 1.0f) {
    return {static_cast<float>(r) / 255.0f,
            static_cast<float>(g) / 255.0f,
            static_cast<float>(b) / 255.0f,
            a};
}

components::theme::ThemeColorTokens theme() {
    components::theme::ThemeColorTokens tokens = darkMode
        ? components::theme::DarkThemeColors()
        : components::theme::LightThemeColors();
    const std::array<eui::Color, 4> accents{{
        rgb(56, 113, 224),
        rgb(15, 148, 136),
        rgb(132, 86, 212),
        rgb(220, 96, 42),
    }};
    tokens.primary = accents[static_cast<std::size_t>(accentIndex)];
    return tokens;
}

eui::Color pageBg() {
    return darkMode ? rgb(17, 20, 27) : rgb(239, 243, 249);
}

eui::Color surface() {
    return darkMode ? rgb(31, 36, 48) : rgb(255, 255, 255);
}

eui::Color softSurface() {
    return darkMode ? rgb(39, 45, 59) : rgb(246, 249, 253);
}

eui::Color primaryText() {
    return darkMode ? rgb(241, 245, 249) : rgb(17, 24, 39);
}

eui::Color mutedText() {
    return darkMode ? rgb(157, 169, 189) : rgb(96, 108, 128);
}

bool containsFolded(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    auto lower = [](std::string value) {
        for (char& ch : value) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    };
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

bool matchesSearch(const Task& task) {
    return containsFolded(task.title, searchQuery) || containsFolded(task.course, searchQuery) || containsFolded(task.due, searchQuery);
}

int openTaskCount() {
    return static_cast<int>(std::count_if(tasks.begin(), tasks.end(), [](const Task& task) { return !task.done; }));
}

float averageProgress() {
    if (tasks.empty()) {
        return 0.0f;
    }
    float total = 0.0f;
    for (const Task& task : tasks) {
        total += task.progress;
    }
    return total / static_cast<float>(tasks.size());
}

void addTask() {
    const std::string base = searchQuery.empty() ? "New study task" : searchQuery;
    tasks.push_back({base, activeNav == 1 ? "Current Course" : "General Study", "Unscheduled", 0.0f, false});
    selectedTask = static_cast<int>(tasks.size()) - 1;
    activeNav = 0;
    activeTab = 1;
}

void text(eui::Ui& ui,
          const std::string& id,
          float w,
          float h,
          const std::string& value,
          float size,
          eui::Color c,
          int weight = 400,
          eui::HorizontalAlign align = eui::HorizontalAlign::Left) {
    components::text(ui, id)
        .size(w, h)
        .text(value)
        .fontSize(size)
        .fontWeight(weight)
        .lineHeight(size + 3.0f)
        .color(c)
        .horizontalAlign(align)
        .verticalAlign(eui::VerticalAlign::Center)
        .build();
}

void card(eui::Ui& ui, const std::string& id, float w, float h, eui::Color fill = rgb(255, 255, 255)) {
    components::panel(ui, id)
        .size(w, h)
        .radius(12.0f)
        .color(fill)
        .border(1.0f, darkMode ? rgb(57, 65, 82) : rgb(221, 227, 237))
        .shadow(12.0f, 0.0f, 5.0f, darkMode ? color(0.0f, 0.0f, 0.0f, 0.18f) : color(0.11f, 0.14f, 0.20f, 0.07f))
        .build();
}

void metricCard(eui::Ui& ui,
                const std::string& id,
                float w,
                const std::string& label,
                const std::string& value,
                const std::string& detail,
                bool active,
                std::function<void()> onClick) {
    ui.stack(id)
        .size(w, 132.0f)
        .content([&] {
            components::panel(ui, id + ".panel")
                .size(w, 132.0f)
                .radius(12.0f)
                .color(active ? theme().primary : surface())
                .border(1.0f, active ? theme().primary : (darkMode ? rgb(57, 65, 82) : rgb(222, 228, 238)))
                .shadow(12.0f, 0.0f, 5.0f, darkMode ? color(0.0f, 0.0f, 0.0f, 0.18f) : color(0.11f, 0.14f, 0.20f, 0.07f))
                .build();

            ui.column(id + ".content")
                .size(w, 132.0f)
                .padding(18.0f)
                .gap(10.0f)
                .content([&] {
                    text(ui, id + ".label", w - 36.0f, 20.0f, label, 14.0f, active ? rgb(238, 244, 255) : mutedText(), 600);
                    text(ui, id + ".value", w - 36.0f, 38.0f, value, 30.0f, active ? rgb(255, 255, 255) : primaryText(), 800);
                    text(ui, id + ".detail", w - 36.0f, 20.0f, detail, 13.0f, active ? rgb(226, 235, 251) : mutedText(), 500);
                })
                .build();

            components::button(ui, id + ".action")
                .size(w, 132.0f)
                .text("")
                .colors(color(0, 0, 0, 0), color(1.0f, 1.0f, 1.0f, active ? 0.10f : 0.06f), color(1.0f, 1.0f, 1.0f, 0.14f))
                .border(0.0f, color(0, 0, 0, 0))
                .shadow(0.0f, 0.0f, 0.0f, color(0, 0, 0, 0))
                .radius(12.0f)
                .onClick(std::move(onClick))
                .build();
        })
        .build();
}

void taskRow(eui::Ui& ui, int index, float w) {
    Task& task = tasks[static_cast<std::size_t>(index)];
    const bool selected = selectedTask == index;
    const float copyW = std::max(120.0f, w - 270.0f);
    ui.stack("task." + std::to_string(index))
        .size(w, 86.0f)
        .content([&] {
            components::panel(ui, "task.panel." + std::to_string(index))
                .size(w, 86.0f)
                .radius(10.0f)
                .color(selected ? (darkMode ? rgb(42, 52, 72) : rgb(238, 245, 255)) : surface())
                .border(1.0f, selected ? theme().primary : (darkMode ? rgb(57, 65, 82) : rgb(224, 229, 238)))
                .build();

            ui.row("task.content." + std::to_string(index))
                .size(w, 86.0f)
                .padding(14.0f)
                .gap(14.0f)
                .alignItems(eui::Align::CENTER)
                .content([&] {
                    components::checkbox(ui, "task.check." + std::to_string(index))
                        .theme(theme())
                        .size(28.0f, 28.0f)
                        .checked(task.done)
                        .onChange([index](bool value) {
                            tasks[static_cast<std::size_t>(index)].done = value;
                            tasks[static_cast<std::size_t>(index)].progress = value ? 1.0f : 0.45f;
                            selectedTask = index;
                        })
                        .build();

                    ui.column("task.copy." + std::to_string(index))
                        .size(copyW, 58.0f)
                        .gap(6.0f)
                        .content([&] {
                            text(ui, "task.title." + std::to_string(index), copyW, 24.0f, task.title, 16.0f, primaryText(), 700);
                            text(ui, "task.course." + std::to_string(index), copyW, 20.0f, task.course + " - " + task.due, 13.0f, mutedText(), 500);
                        })
                        .build();

                    components::progress(ui, "task.progress." + std::to_string(index))
                        .theme(theme())
                        .size(96.0f, 8.0f)
                        .value(task.progress)
                        .build();

                    components::button(ui, "task.open." + std::to_string(index))
                        .theme(theme(), false)
                        .size(68.0f, 36.0f)
                        .text("Open")
                        .fontSize(13.0f)
                        .onClick([index] {
                            selectedTask = index;
                            activeNav = 1;
                        })
                        .build();
                })
                .build();
        })
        .build();
}

void sidebar(eui::Ui& ui, float w, float h) {
    ui.stack("sidebar")
        .size(w, h)
        .content([&] {
            card(ui, "sidebar.panel", w, h, rgb(27, 32, 43));
            ui.column("sidebar.content")
                .size(w, h)
                .padding(16.0f)
                .gap(14.0f)
                .content([&] {
                    text(ui, "sidebar.brand", w - 32.0f, 40.0f, "EUI LMS", 22.0f, rgb(248, 250, 252), 800, eui::HorizontalAlign::Center);
                    const std::array<const char*, 4> labels{{"Dashboard", "Courses", "Schedule", "Settings"}};
                    for (int i = 0; i < 4; ++i) {
                        components::button(ui, "nav." + std::to_string(i))
                            .size(w - 32.0f, 44.0f)
                            .text(labels[static_cast<std::size_t>(i)])
                            .fontSize(14.0f)
                            .colors(activeNav == i ? theme().primary : color(0, 0, 0, 0),
                                    activeNav == i ? components::theme::buttonHover(theme(), theme().primary) : color(1, 1, 1, 0.08f),
                                    activeNav == i ? components::theme::buttonPressed(theme(), theme().primary) : color(1, 1, 1, 0.13f))
                            .textColor(rgb(245, 248, 255))
                            .border(0.0f, color(0, 0, 0, 0))
                            .shadow(0.0f, 0.0f, 0.0f, color(0, 0, 0, 0))
                            .radius(10.0f)
                            .onClick([i] {
                                activeNav = i;
                                activeTab = i == 2 ? 1 : 0;
                            })
                            .build();
                    }
                })
                .build();
        })
        .build();
}

void taskListCard(eui::Ui& ui, const std::string& id, float w, float h, const std::string& title) {
    ui.stack(id)
        .size(w, h)
        .content([&] {
            card(ui, id + ".panel", w, h, surface());
            ui.column(id + ".content")
                .size(w, h)
                .padding(20.0f)
                .gap(14.0f)
                .content([&] {
                    ui.row(id + ".header")
                        .size(w - 40.0f, 42.0f)
                        .gap(14.0f)
                        .alignItems(eui::Align::CENTER)
                        .content([&] {
                            text(ui, id + ".title", w - 264.0f, 34.0f, title, 24.0f, primaryText(), 800);
                            components::segmented(ui, id + ".range")
                                .theme(theme())
                                .size(210.0f, 34.0f)
                                .items({"Day", "Week", "Month"})
                                .selected(rangeIndex)
                                .fontSize(13.0f)
                                .onChange([](int index) { rangeIndex = index; })
                                .build();
                        })
                        .build();

                    int visible = 0;
                    for (int i = 0; i < static_cast<int>(tasks.size()); ++i) {
                        if (!matchesSearch(tasks[static_cast<std::size_t>(i)])) {
                            continue;
                        }
                        taskRow(ui, i, w - 40.0f);
                        ++visible;
                    }

                    if (visible == 0) {
                        text(ui, id + ".empty", w - 40.0f, 44.0f, "No matching tasks. Use Add Task to create one.", 16.0f, mutedText(), 600, eui::HorizontalAlign::Center);
                    }
                })
                .build();
        })
        .build();
}

void detailCard(eui::Ui& ui, float w, float h) {
    selectedTask = std::clamp(selectedTask, 0, std::max(0, static_cast<int>(tasks.size()) - 1));
    Task& task = tasks[static_cast<std::size_t>(selectedTask)];
    ui.stack("detail.card")
        .size(w, h)
        .content([&] {
            card(ui, "detail.panel", w, h, surface());
            ui.column("detail.content")
                .size(w, h)
                .padding(20.0f)
                .gap(12.0f)
                .content([&] {
                    text(ui, "detail.title", w - 40.0f, 30.0f, "Selected Task", 21.0f, primaryText(), 800);
                    text(ui, "detail.name", w - 40.0f, 28.0f, task.title, 18.0f, primaryText(), 700);
                    text(ui, "detail.course", w - 40.0f, 24.0f, task.course + " - " + task.due, 14.0f, mutedText(), 500);
                    components::progress(ui, "detail.progress")
                        .theme(theme())
                        .size(w - 40.0f, 10.0f)
                        .value(task.progress)
                        .build();
                    components::button(ui, "detail.primary")
                        .theme(theme(), true)
                        .size(w - 40.0f, 42.0f)
                        .text(task.done ? "Reopen Task" : "Continue")
                        .fontSize(14.0f)
                        .onClick([] {
                            Task& selected = tasks[static_cast<std::size_t>(selectedTask)];
                            if (selected.done) {
                                selected.done = false;
                                selected.progress = 0.65f;
                                return;
                            }
                            selected.progress = std::min(1.0f, selected.progress + 0.15f);
                            selected.done = selected.progress >= 1.0f;
                        })
                        .build();
                    components::button(ui, "detail.schedule")
                        .theme(theme(), false)
                        .size(w - 40.0f, 38.0f)
                        .text("Open Schedule")
                        .fontSize(13.0f)
                        .onClick([] { activeNav = 2; })
                        .build();
                })
                .build();
        })
        .build();
}

void dashboardBody(eui::Ui& ui, float w, float h) {
    const float leftW = std::max(420.0f, w * 0.62f);
    const float rightW = std::max(280.0f, w - leftW - 20.0f);
    const float metricW = std::max(150.0f, (leftW - 36.0f) / 3.0f);
    const int open = openTaskCount();
    const int completed = static_cast<int>(tasks.size()) - open;
    const int average = static_cast<int>(averageProgress() * 100.0f);

    ui.row("body.layout")
        .size(w, h)
        .gap(20.0f)
        .content([&] {
            ui.column("body.left")
                .size(leftW, h)
                .gap(18.0f)
                .content([&] {
                    ui.row("metrics")
                        .size(leftW, 132.0f)
                        .gap(18.0f)
                        .content([&] {
                            metricCard(ui, "metric.tasks", metricW, "Open tasks", std::to_string(open), std::to_string(completed) + " completed", activeTab == 0, [] { activeTab = 0; });
                            metricCard(ui, "metric.goal", metricW, "Weekly goal", std::to_string(static_cast<int>(weeklyGoal * 100.0f)) + "%", focusMode ? "Focus mode on" : "Focus mode off", activeTab == 2, [] { activeTab = 2; });
                            metricCard(ui, "metric.score", metricW, "Progress", std::to_string(average) + "%", reminders ? "Reminders on" : "Reminders off", activeTab == 1, [] { activeTab = 1; });
                        })
                        .build();
                    taskListCard(ui, "tasks", leftW, h - 150.0f, activeTab == 1 ? "Course Work" : "Today");
                })
                .build();

            ui.column("body.right")
                .size(rightW, h)
                .gap(18.0f)
                .content([&] {
                    ui.stack("progress.card")
                        .size(rightW, 210.0f)
                        .content([&] {
                            card(ui, "progress.panel", rightW, 210.0f, surface());
                            ui.column("progress.content")
                                .size(rightW, 210.0f)
                                .padding(20.0f)
                                .gap(16.0f)
                                .content([&] {
                                    text(ui, "progress.title", rightW - 40.0f, 30.0f, "Weekly Goal", 22.0f, primaryText(), 800);
                                    components::progress(ui, "progress.week").theme(theme()).size(rightW - 40.0f, 14.0f).value(weeklyGoal).build();
                                    components::slider(ui, "progress.slider").theme(theme()).size(rightW - 40.0f, 32.0f).value(weeklyGoal).onChange([](float value) { weeklyGoal = value; }).build();
                                    text(ui, "progress.copy", rightW - 40.0f, 42.0f, std::to_string(static_cast<int>(weeklyGoal * 100.0f)) + "% complete", 16.0f, mutedText(), 700);
                                })
                                .build();
                        })
                        .build();
                    detailCard(ui, rightW, h - 228.0f);
                })
                .build();
        })
        .build();
}

void coursesBody(eui::Ui& ui, float w, float h) {
    const float leftW = std::max(440.0f, w * 0.56f);
    ui.row("courses.body")
        .size(w, h)
        .gap(20.0f)
        .content([&] {
            taskListCard(ui, "courses.tasks", leftW, h, "Courses");
            detailCard(ui, w - leftW - 20.0f, h);
        })
        .build();
}

void scheduleBody(eui::Ui& ui, float w, float h) {
    ui.stack("schedule.card")
        .size(w, h)
        .content([&] {
            card(ui, "schedule.panel", w, h, surface());
            ui.column("schedule.content")
                .size(w, h)
                .padding(22.0f)
                .gap(14.0f)
                .content([&] {
                    text(ui, "schedule.title", w - 44.0f, 36.0f, "Schedule", 26.0f, primaryText(), 800);
                    for (int i = 0; i < static_cast<int>(tasks.size()); ++i) {
                        const Task& task = tasks[static_cast<std::size_t>(i)];
                        if (!matchesSearch(task)) {
                            continue;
                        }
                        ui.row("schedule.row." + std::to_string(i))
                            .size(w - 44.0f, 68.0f)
                            .gap(16.0f)
                            .alignItems(eui::Align::CENTER)
                            .content([&] {
                                text(ui, "schedule.due." + std::to_string(i), 150.0f, 28.0f, task.due, 15.0f, theme().primary, 700);
                                text(ui, "schedule.task." + std::to_string(i), w - 360.0f, 30.0f, task.title + " / " + task.course, 16.0f, primaryText(), 700);
                                components::button(ui, "schedule.open." + std::to_string(i))
                                    .theme(theme(), false)
                                    .size(120.0f, 38.0f)
                                    .text(task.done ? "Review" : "Open")
                                    .fontSize(13.0f)
                                    .onClick([i] {
                                        selectedTask = i;
                                        activeNav = 1;
                                    })
                                    .build();
                            })
                            .build();
                    }
                })
                .build();
        })
        .build();
}

void settingsBody(eui::Ui& ui, float w, float h) {
    ui.stack("settings.page")
        .size(w, h)
        .content([&] {
            card(ui, "settings.page.panel", w, h, surface());
            ui.column("settings.page.content")
                .size(w, h)
                .padding(24.0f)
                .gap(18.0f)
                .content([&] {
                    text(ui, "settings.page.title", w - 48.0f, 38.0f, "Settings", 28.0f, primaryText(), 800);
                    components::toggleSwitch(ui, "settings.dark")
                        .theme(theme())
                        .size(320.0f, 34.0f)
                        .checked(darkMode)
                        .label("Night mode")
                        .onChange([](bool value) { darkMode = value; })
                        .build();
                    components::segmented(ui, "settings.accent")
                        .theme(theme())
                        .size(420.0f, 40.0f)
                        .items({"Blue", "Teal", "Purple", "Orange"})
                        .selected(accentIndex)
                        .onChange([](int index) { accentIndex = index; })
                        .build();
                    components::toggleSwitch(ui, "settings.focus.mode")
                        .theme(theme())
                        .size(320.0f, 34.0f)
                        .checked(focusMode)
                        .label("Focus mode")
                        .onChange([](bool value) { focusMode = value; })
                        .build();
                    components::toggleSwitch(ui, "settings.reminder.mode")
                        .theme(theme())
                        .size(320.0f, 34.0f)
                        .checked(reminders)
                        .label("Reminders")
                        .onChange([](bool value) { reminders = value; })
                        .build();
                    components::button(ui, "settings.reset")
                        .theme(theme(), false)
                        .size(220.0f, 42.0f)
                        .text("Reset Demo Data")
                        .fontSize(14.0f)
                        .onClick([] {
                            searchQuery.clear();
                            selectedTask = 0;
                            weeklyGoal = 0.68f;
                            for (Task& task : tasks) {
                                task.done = false;
                                task.progress = std::min(task.progress, 0.82f);
                            }
                        })
                        .build();
                })
                .build();
        })
        .build();
}

void pageBody(eui::Ui& ui, float w, float h) {
    if (activeNav == 1) {
        coursesBody(ui, w, h);
    } else if (activeNav == 2) {
        scheduleBody(ui, w, h);
    } else if (activeNav == 3) {
        settingsBody(ui, w, h);
    } else {
        dashboardBody(ui, w, h);
    }
}

} // namespace

const DslAppConfig& dslAppConfig() {
    static const DslAppConfig config = DslAppConfig{}
        .title("LMS Dashboard")
        .pageId("lms-dashboard")
        .clearColor(rgb(239, 243, 249))
        .windowSize(1280, 820)
        .fps(60.0)
        .showDebugStatsInTitle(false);
    return config;
}

void compose(eui::Ui& ui, const eui::Screen& screen) {
    const float rootW = screen.width;
    const float rootH = screen.height;
    const float sidebarW = 184.0f;
    const float pad = 20.0f;
    const float contentW = std::max(720.0f, rootW - sidebarW - pad * 3.0f);
    const float contentH = std::max(620.0f, rootH - pad * 2.0f);

    ui.stack("root")
        .size(rootW, rootH)
        .content([&] {
            ui.rect("background").size(rootW, rootH).color(pageBg()).build();

            ui.row("app")
                .x(pad)
                .y(pad)
                .size(rootW - pad * 2.0f, rootH - pad * 2.0f)
                .gap(20.0f)
                .content([&] {
                    sidebar(ui, sidebarW, contentH);

                    ui.column("main")
                        .size(contentW, contentH)
                        .gap(18.0f)
                        .content([&] {
                            ui.row("topbar")
                                .size(contentW, 52.0f)
                                .gap(14.0f)
                                .alignItems(eui::Align::CENTER)
                                .content([&] {
                                    components::input(ui, "search")
                                        .theme(theme())
                                        .size(std::max(300.0f, contentW - 514.0f), 44.0f)
                                        .value(searchQuery)
                                        .placeholder("Search courses, tasks, notes")
                                        .onChange([](const std::string& value) { searchQuery = value; })
                                        .build();

                                    components::tabs(ui, "main.tabs")
                                        .theme(theme())
                                        .size(300.0f, 44.0f)
                                        .items({"Overview", "Work", "Analytics"})
                                        .selected(activeTab)
                                        .fontSize(14.0f)
                                        .onChange([](int index) { activeTab = index; })
                                        .build();

                                    components::button(ui, "new.task")
                                        .theme(theme(), true)
                                        .size(172.0f, 44.0f)
                                        .text("Add Task")
                                        .fontSize(14.0f)
                                        .onClick([] { addTask(); })
                                        .build();
                                })
                                .build();

                            pageBody(ui, contentW, contentH - 70.0f);
                        })
                        .build();
                })
                .build();
        })
        .build();
}

} // namespace app
