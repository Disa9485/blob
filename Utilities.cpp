// Utilities.cpp
#include "Utilities.hpp"

#include "ChatUI.hpp"
#include "GameRenderer.hpp"
#include "LlamaChatLoader.hpp"

#include <glad/glad.h>
#include <imgui.h>

#include <algorithm>
#include <vector>

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
    #include <psapi.h>
#endif

#include "stb_image.h"

namespace util {

bool setWindowIcons(GLFWwindow* window)
{
    const std::vector<std::string> paths = {
        "assets/icon/robot_icon_16.png",
        "assets/icon/robot_icon_24.png",
        "assets/icon/robot_icon_32.png",
        "assets/icon/robot_icon_48.png",
        "assets/icon/robot_icon_64.png",
        "assets/icon/robot_icon_128.png",
        "assets/icon/robot_icon_256.png",
        "assets/icon/robot_icon_1024.png"
    };

    std::vector<GLFWimage> images;
    std::vector<unsigned char*> pixel_data;

    for (const auto& path : paths)
    {
        int w, h, channels;
        unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);

        if (!pixels) {
            continue;
        }

        GLFWimage img;
        img.width = w;
        img.height = h;
        img.pixels = pixels;

        images.push_back(img);
        pixel_data.push_back(pixels);
    }

    if (images.empty()) {
        return false;
    }

    glfwSetWindowIcon(window, static_cast<int>(images.size()), images.data());

    for (auto* p : pixel_data) {
        stbi_image_free(p);
    }

    return true;
}

bool recreateWindowAndGraphics(
    GLFWwindow*& window,
    ImGuiLayer& imgui,
    std::unique_ptr<render::GameRenderer>& game_renderer,
    const WindowCreateParams& params,
    std::string& error)
{
    if (game_renderer) {
        game_renderer.reset();
    }

    imgui.shutdown();

    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, params.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, params.msaa_samples);

    GLFWmonitor* monitor = params.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;

    const int create_width = (params.fullscreen && mode) ? mode->width : params.width;
    const int create_height = (params.fullscreen && mode) ? mode->height : params.height;

    window = glfwCreateWindow(
        create_width,
        create_height,
        params.title,
        monitor,
        nullptr
    );

    if (!window) {
        error = "Failed to recreate GLFW window.";
        return false;
    }

    if (!params.fullscreen && params.use_position) {
        glfwSetWindowPos(window, params.pos_x, params.pos_y);
    }

    setWindowIcons(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(params.vsync ? 1 : 0);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        error = "Failed to reload OpenGL function pointers.";
        glfwDestroyWindow(window);
        window = nullptr;
        return false;
    }

    if (!imgui.initialize(window, "#version 330")) {
        error = "Failed to reinitialize Dear ImGui.";
        glfwDestroyWindow(window);
        window = nullptr;
        return false;
    }

    game_renderer = std::make_unique<render::GameRenderer>();
    if (!game_renderer->initialize(error)) {
        imgui.shutdown();
        glfwDestroyWindow(window);
        window = nullptr;
        return false;
    }

    return true;
}

#ifdef _WIN32
static inline std::uint64_t fileTimeToUint64(const FILETIME& ft) {
    ULARGE_INTEGER li{};
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart;
}
#endif

void DebugPerfSampler::update(double dt_seconds) {
    frame_ms_ = static_cast<float>(dt_seconds * 1000.0);
    fps_accum_ += dt_seconds;
    ++fps_frames_;

    if (fps_accum_ >= 0.5) {
        fps_ = static_cast<float>(fps_frames_ / fps_accum_);
        fps_accum_ = 0.0;
        fps_frames_ = 0;
    }

#ifdef _WIN32
    cpu_mem_accum_ += dt_seconds;
    if (cpu_mem_accum_ >= 0.5) {
        sampleWindowsProcessStats();
        cpu_mem_accum_ = 0.0;
    }
#endif
}

DebugPerfStats DebugPerfSampler::snapshot() const {
    DebugPerfStats s;
    s.fps = fps_;
    s.frame_ms = frame_ms_;
    s.cpu_percent = cpu_percent_;
    s.working_set_bytes = working_set_bytes_;
    s.private_bytes = private_bytes_;
    return s;
}

void DebugPerfSampler::sampleWindowsProcessStats() {
#ifdef _WIN32
    HANDLE process = GetCurrentProcess();

    FILETIME create_time{}, exit_time{}, kernel_time{}, user_time{};
    if (GetProcessTimes(process, &create_time, &exit_time, &kernel_time, &user_time)) {
        FILETIME idle_time_sys{}, kernel_time_sys{}, user_time_sys{};
        if (GetSystemTimes(&idle_time_sys, &kernel_time_sys, &user_time_sys)) {
            const std::uint64_t proc_now =
                fileTimeToUint64(kernel_time) + fileTimeToUint64(user_time);
            const std::uint64_t sys_now =
                fileTimeToUint64(kernel_time_sys) + fileTimeToUint64(user_time_sys);

            if (have_prev_cpu_sample_) {
                const std::uint64_t proc_delta = proc_now - prev_proc_cpu_time_;
                const std::uint64_t sys_delta = sys_now - prev_sys_cpu_time_;

                if (sys_delta > 0) {
                    const double normalized =
                        (static_cast<double>(proc_delta) / static_cast<double>(sys_delta)) * 100.0;
                    cpu_percent_ = static_cast<float>(normalized);
                }
            }

            prev_proc_cpu_time_ = proc_now;
            prev_sys_cpu_time_ = sys_now;
            have_prev_cpu_sample_ = true;
        }
    }

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(
            process,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
            sizeof(pmc))) {
        working_set_bytes_ = static_cast<std::uint64_t>(pmc.WorkingSetSize);
        private_bytes_ = static_cast<std::uint64_t>(pmc.PrivateUsage);
    }
#endif
}

void DebugFrameBreakdown::beginFrame() {
    current_ = DebugPerfStats{};
}

void DebugFrameBreakdown::setBaseStats(const DebugPerfStats& base) {
    current_.fps = base.fps;
    current_.frame_ms = base.frame_ms;
    current_.cpu_percent = base.cpu_percent;
    current_.working_set_bytes = base.working_set_bytes;
    current_.private_bytes = base.private_bytes;
}

void DebugFrameBreakdown::finalizeFrame() {
    latest_ = current_;
}

DebugPerfStats DebugFrameBreakdown::snapshot() const {
    return latest_;
}

void DebugFrameBreakdown::setInputMs(float v) { current_.input_ms = v; }
void DebugFrameBreakdown::setSpeechMs(float v) { current_.speech_ms = v; }
void DebugFrameBreakdown::setTouchMs(float v) { current_.touch_ms = v; }
void DebugFrameBreakdown::setPhysicsMs(float v) { current_.physics_ms = v; }
void DebugFrameBreakdown::setAutonomousMs(float v) { current_.autonomous_ms = v; }
void DebugFrameBreakdown::setEyeMs(float v) { current_.eye_ms = v; }
void DebugFrameBreakdown::setSoftMeshMs(float v) { current_.soft_mesh_ms = v; }
void DebugFrameBreakdown::setRenderMs(float v) { current_.render_ms = v; }
void DebugFrameBreakdown::setUiMs(float v) { current_.ui_ms = v; }
void DebugFrameBreakdown::setSwapMs(float v) { current_.swap_ms = v; }

ScopedTimerMs::ScopedTimerMs(float* out) : out_ms(out) {}

ScopedTimerMs::~ScopedTimerMs() {
    if (out_ms) {
        const auto end = Clock::now();
        *out_ms = std::chrono::duration<float, std::milli>(end - start).count();
    }
}

double bytesToMiB(std::uint64_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

void drawDebugOverlay(
    const DebugPerfStats& stats,
    const AppConfig& config,
    const physics::PsdAssembly& assembly,
    const physics::PhysicsScene* physics_scene,
    const ChatUI* chat_ui,
    const LlamaChatLoader* loader,
    int display_w,
    int display_h)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return;
    }

    int visible_parts = 0;
    int visible_overlays = 0;
    int soft_parts = 0;
    int rigid_parts = 0;

    for (const auto& part : assembly.parts) {
        if (part.visible) {
            ++visible_parts;
        }

        if (part.kind == physics::PartKind::Soft) {
            ++soft_parts;
            for (const auto& overlay : part.overlays) {
                if (overlay.visible) {
                    ++visible_overlays;
                }
            }
        } else {
            ++rigid_parts;
        }
    }

    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x - 12.0f,
            viewport->WorkPos.y + 12.0f
        ),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f)
    );

    ImGui::Begin(
        "##DebugOverlay",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav
    );

    ImGui::Text("Performance");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", stats.fps);
    ImGui::Text("Frame: %.2f ms", stats.frame_ms);
    ImGui::Text("CPU: %.1f%%", stats.cpu_percent);
    ImGui::Text("RAM (working set): %.1f MiB", bytesToMiB(stats.working_set_bytes));
    ImGui::Text("RAM (private): %.1f MiB", bytesToMiB(stats.private_bytes));

    ImGui::Separator();
    ImGui::Text("Window: %dx%d", config.window.width, config.window.height);
    ImGui::Text("Framebuffer: %dx%d", display_w, display_h);
    ImGui::Text("VSync: %s", config.window.vsync ? "On" : "Off");
    ImGui::Text("MSAA: %d", config.window.anti_aliasing_samples);

    ImGui::Separator();
    ImGui::Text("Breakdown (ms)");
    ImGui::Text("Input: %.3f", stats.input_ms);
    ImGui::Text("Speech: %.3f", stats.speech_ms);
    ImGui::Text("Touch: %.3f", stats.touch_ms);
    ImGui::Text("Physics: %.3f", stats.physics_ms);
    ImGui::Text("Autonomous: %.3f", stats.autonomous_ms);
    ImGui::Text("Eyes: %.3f", stats.eye_ms);
    ImGui::Text("Soft mesh: %.3f", stats.soft_mesh_ms);
    ImGui::Text("Render: %.3f", stats.render_ms);
    ImGui::Text("UI: %.3f", stats.ui_ms);
    ImGui::Text("Swap: %.3f", stats.swap_ms);

    ImGui::Separator();
    ImGui::Text("Scene parts: %d", static_cast<int>(assembly.parts.size()));
    ImGui::Text("Visible parts: %d", visible_parts);
    ImGui::Text("Visible overlays: %d", visible_overlays);
    ImGui::Text("Soft / Rigid: %d / %d", soft_parts, rigid_parts);
    ImGui::Text("Render items: %d", static_cast<int>(assembly.renderItems.size()));
    ImGui::Text("Joints: %d", static_cast<int>(assembly.joints.size()));

    ImGui::Separator();
    ImGui::Text("LLM loading: %s", (loader && !loader->isReady() && !loader->hasFailed()) ? "Yes" : "No");
    ImGui::Text("LLM generating: %s", (chat_ui && chat_ui->isGenerating()) ? "Yes" : "No");
    ImGui::Text("Physics: %s", physics_scene ? "Ready" : "No");

    ImGui::End();
}

} // namespace util