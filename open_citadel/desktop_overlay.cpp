#include "desktop_overlay.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

namespace open_citadel::desktop_overlay {

namespace {

std::atomic<bool> g_open{false};
std::atomic<int> g_swap_interval_request{-1};
std::mutex g_queue_mutex;
std::deque<SDL_Event> g_events;
std::deque<Command> g_commands;
Snapshot g_snapshot;

ImGuiContext *g_context = nullptr;
bool g_ui_settings_initialized = false;
std::uint64_t g_ui_revision = 0;
UserSettings g_ui_settings;
int g_ui_width = 1280;
int g_ui_height = 720;
bool g_sensitivity_edited = false;
double g_fps = 0.0;
double g_frame_time_ms = 0.0;
Uint64 g_sample_start = 0;
Uint64 g_sample_frames = 0;

void push_command(const Command &command)
{
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    if (g_commands.size() >= 64)
        g_commands.pop_front();
    g_commands.push_back(command);
}

Snapshot copy_snapshot()
{
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    return g_snapshot;
}

void drain_events(std::vector<SDL_Event> *events)
{
    if (!events)
        return;
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    events->assign(g_events.begin(), g_events.end());
    g_events.clear();
}

void draw_controls_tab(const Snapshot &snapshot)
{
    ImGui::TextWrapped("Movement is sent to the game's virtual left stick.");
    ImGui::Spacing();

    struct Binding {
        const char *label;
        const std::string *key;
        MovementKey movement;
    };
    const Binding bindings[] = {
        {"Forward", &g_ui_settings.move_forward, MovementKey::Forward},
        {"Backward", &g_ui_settings.move_backward, MovementKey::Backward},
        {"Left", &g_ui_settings.move_left, MovementKey::Left},
        {"Right", &g_ui_settings.move_right, MovementKey::Right},
    };

    for (const Binding &binding : bindings) {
        ImGui::PushID(static_cast<int>(binding.movement));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%-9s %s", binding.label, binding.key->c_str());
        ImGui::SameLine(190.0f);
        if (ImGui::Button("Rebind")) {
            Command command;
            command.type = CommandType::BeginRebind;
            command.movement_key = binding.movement;
            push_command(command);
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (ImGui::Button("Reset movement keys to WASD")) {
        Command command;
        command.type = CommandType::ResetMovement;
        push_command(command);
    }
    ImGui::TextDisabled("Click Rebind, then press one key. Escape cancels.");
}

void draw_game_tab(const Snapshot &snapshot)
{
    ImGui::Text("Live performance");
    ImGui::Separator();
    ImGui::Text("%.0f FPS  |  %.2f ms/frame", g_fps, g_frame_time_ms);
    ImGui::Text("VSync active now: %s", snapshot.active_vsync ? "on" : "off");
    ImGui::Text("Game 60 FPS cap active now: %s",
                snapshot.active_uncap_fps ? "no" : "yes");
    ImGui::Spacing();

    if (ImGui::Checkbox("VSync", &g_ui_settings.vsync)) {
        Command command;
        command.type = CommandType::SetVsync;
        command.enabled = g_ui_settings.vsync;
        push_command(command);
    }
    if (ImGui::Checkbox("Disable the game's 60 FPS cap next launch",
                        &g_ui_settings.uncap_fps)) {
        Command command;
        command.type = CommandType::SetUncapFps;
        command.enabled = g_ui_settings.uncap_fps;
        push_command(command);
    }
    if (ImGui::Checkbox("Uncapped benchmark mode next launch",
                        &g_ui_settings.uncapped_benchmark)) {
        Command command;
        command.type = CommandType::SetUncappedBenchmark;
        command.enabled = g_ui_settings.uncapped_benchmark;
        push_command(command);
    }
    ImGui::TextDisabled("Benchmark mode also disables VSync for that run.");
    ImGui::Spacing();

    if (ImGui::SliderFloat("Mouse sensitivity",
                           &g_ui_settings.mouse_sensitivity,
                           0.1f, 4.0f, "%.1f"))
        g_sensitivity_edited = true;
    if (ImGui::IsItemDeactivatedAfterEdit() && g_sensitivity_edited) {
        Command command;
        command.type = CommandType::SetMouseSensitivity;
        command.value = g_ui_settings.mouse_sensitivity;
        push_command(command);
        g_sensitivity_edited = false;
    }
    if (ImGui::Checkbox("Invert vertical mouse look",
                        &g_ui_settings.invert_mouse_y)) {
        Command command;
        command.type = CommandType::SetInvertMouseY;
        command.enabled = g_ui_settings.invert_mouse_y;
        push_command(command);
    }
}

void draw_display_tab()
{
    ImGui::Text("Next launch");
    ImGui::Separator();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputInt("Window width", &g_ui_width, 10, 100);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputInt("Window height", &g_ui_height, 10, 100);
    if (g_ui_width < 320 || g_ui_width > 7680 ||
        g_ui_height < 240 || g_ui_height > 4320) {
        ImGui::TextColored(ImVec4(1.0f, 0.48f, 0.42f, 1.0f),
                           "Valid range: 320x240 through 7680x4320.");
    } else if (ImGui::Button("Save window size")) {
        Command command;
        command.type = CommandType::SaveResolution;
        command.width = g_ui_width;
        command.height = g_ui_height;
        push_command(command);
    }

    if (ImGui::Checkbox("Start fullscreen next launch",
                        &g_ui_settings.fullscreen)) {
        Command command;
        command.type = CommandType::SetFullscreen;
        command.enabled = g_ui_settings.fullscreen;
        push_command(command);
    }
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Custom sizes and aspect ratios are accepted. Window dimensions and "
        "fullscreen mode take effect after restarting the game.");
}

bool initialize(SDL_Window *window)
{
    if (g_context)
        return true;

    IMGUI_CHECKVERSION();
    g_context = ImGui::CreateContext();
    if (!g_context) {
        fprintf(stderr, "OpenCitadel: could not allocate the ImGui context\n");
        set_open(false);
        return false;
    }
    ImGui::SetCurrentContext(g_context);

    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 7.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(18.0f, 16.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.075f, 0.10f, 0.97f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.40f, 0.49f, 0.82f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.13f, 0.30f, 0.37f, 0.95f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.48f, 0.56f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.83f, 0.77f, 1.0f);

    if (!ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext())) {
        fprintf(stderr, "OpenCitadel: ImGui SDL2 initialization failed\n");
        ImGui::DestroyContext(g_context);
        g_context = nullptr;
        set_open(false);
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 100")) {
        fprintf(stderr, "OpenCitadel: ImGui OpenGL ES 2 initialization failed\n");
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(g_context);
        g_context = nullptr;
        set_open(false);
        return false;
    }
    return true;
}

void update_frame_rate()
{
    const Uint64 now = SDL_GetPerformanceCounter();
    if (!g_sample_start) {
        g_sample_start = now;
        g_sample_frames = 0;
    }
    ++g_sample_frames;
    const Uint64 elapsed = now - g_sample_start;
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    if (frequency && elapsed >= frequency / 2) {
        g_fps = static_cast<double>(g_sample_frames) * frequency / elapsed;
        g_frame_time_ms = 1000.0 * elapsed /
                          (static_cast<double>(frequency) * g_sample_frames);
        g_sample_frames = 0;
        g_sample_start = now;
    }
}

} // namespace

void set_open(bool open)
{
    g_open.store(open, std::memory_order_release);
    if (!open) {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        g_events.clear();
    }
}

bool is_open()
{
    return g_open.load(std::memory_order_acquire);
}

void push_event(const SDL_Event &event)
{
    if (!is_open())
        return;
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    if (g_events.size() >= 1024) {
        const auto motion = std::find_if(
            g_events.begin(), g_events.end(), [](const SDL_Event &queued) {
                return queued.type == SDL_MOUSEMOTION;
            });
        if (motion != g_events.end())
            g_events.erase(motion);
        else if (event.type == SDL_MOUSEMOTION)
            return;
    }
    g_events.push_back(event);
}

bool pop_command(Command *command)
{
    if (!command)
        return false;
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    if (g_commands.empty())
        return false;
    *command = g_commands.front();
    g_commands.pop_front();
    return true;
}

void publish_snapshot(const Snapshot &snapshot)
{
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    g_snapshot = snapshot;
}

void request_swap_interval(bool enabled)
{
    g_swap_interval_request.store(enabled ? 1 : 0, std::memory_order_release);
}

void render(SDL_Window *window)
{
    if (!window)
        return;
    const int swap_interval =
        g_swap_interval_request.exchange(-1, std::memory_order_acq_rel);
    if (swap_interval >= 0 && SDL_GL_SetSwapInterval(swap_interval) != 0)
        fprintf(stderr, "OpenCitadel: swap interval change failed: %s\n",
                SDL_GetError());
    if (!is_open() || !initialize(window))
        return;
    ImGui::SetCurrentContext(g_context);

    std::vector<SDL_Event> events;
    drain_events(&events);
    for (const SDL_Event &event : events)
        ImGui_ImplSDL2_ProcessEvent(&event);

    const Snapshot snapshot = copy_snapshot();
    if (!g_ui_settings_initialized) {
        g_ui_settings = snapshot.settings;
        g_ui_width = snapshot.settings.width;
        g_ui_height = snapshot.settings.height;
        g_ui_revision = snapshot.revision;
        g_ui_settings_initialized = true;
    } else if (snapshot.revision != g_ui_revision) {
        if (snapshot.settings.width != g_ui_settings.width ||
            snapshot.settings.height != g_ui_settings.height) {
            g_ui_width = snapshot.settings.width;
            g_ui_height = snapshot.settings.height;
        }
        g_ui_settings = snapshot.settings;
        g_ui_revision = snapshot.revision;
    }

    update_frame_rate();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    bool visible = true;
    ImGui::SetNextWindowSize(ImVec2(500.0f, 440.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(34.0f, 34.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Epic Citadel | Open Citadel", &visible,
                 ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored(ImVec4(0.35f, 0.83f, 0.77f, 1.0f),
                       "Desktop settings");
    ImGui::SameLine();
    ImGui::TextDisabled("F2 or Escape closes");
    if (ImGui::BeginTabBar("desktop-settings")) {
        if (ImGui::BeginTabItem("Game")) {
            draw_game_tab(snapshot);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls")) {
            draw_controls_tab(snapshot);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Display")) {
            draw_display_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!snapshot.rebind_status.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(snapshot.waiting_for_rebind
                               ? ImVec4(1.0f, 0.82f, 0.36f, 1.0f)
                               : ImVec4(0.56f, 0.83f, 0.72f, 1.0f),
                           "%s", snapshot.rebind_status.c_str());
    }
    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!visible)
        set_open(false);
}

void shutdown()
{
    if (!g_context)
        return;
    ImGui::SetCurrentContext(g_context);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(g_context);
    g_context = nullptr;
    g_ui_settings_initialized = false;
    g_sample_start = 0;
    g_sample_frames = 0;
}

} // namespace open_citadel::desktop_overlay
