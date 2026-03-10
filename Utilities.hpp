// Utilities.hpp
#pragma once

#include "AppConfig.hpp"
#include "ImGuiLayer.hpp"
#include "PhysicsScene.hpp"
#include "PsdAssembler.hpp"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

class ChatUI;
class LlamaChatLoader;

namespace render {
class GameRenderer;
}

namespace util {

struct WindowCreateParams {
    int width = 1280;
    int height = 720;
    const char* title = "Window";
    bool fullscreen = false;
    bool resizable = false;
    bool vsync = true;
    int msaa_samples = 0;
    bool use_position = false;
    int pos_x = 0;
    int pos_y = 0;
};

bool setWindowIcons(GLFWwindow* window);

bool recreateWindowAndGraphics(
    GLFWwindow*& window,
    ImGuiLayer& imgui,
    std::unique_ptr<render::GameRenderer>& game_renderer,
    const WindowCreateParams& params,
    std::string& error);

struct DebugPerfStats {
    float fps = 0.0f;
    float frame_ms = 0.0f;
    float cpu_percent = 0.0f;
    std::uint64_t working_set_bytes = 0;
    std::uint64_t private_bytes = 0;

    float input_ms = 0.0f;
    float speech_ms = 0.0f;
    float touch_ms = 0.0f;
    float physics_ms = 0.0f;
    float autonomous_ms = 0.0f;
    float eye_ms = 0.0f;
    float soft_mesh_ms = 0.0f;
    float render_ms = 0.0f;
    float ui_ms = 0.0f;
    float swap_ms = 0.0f;
};

class DebugPerfSampler {
public:
    void update(double dt_seconds);
    DebugPerfStats snapshot() const;

private:
    void sampleWindowsProcessStats();

private:
    double fps_accum_ = 0.0;
    int fps_frames_ = 0;
    double cpu_mem_accum_ = 0.0;

    float fps_ = 0.0f;
    float frame_ms_ = 0.0f;
    float cpu_percent_ = 0.0f;
    std::uint64_t working_set_bytes_ = 0;
    std::uint64_t private_bytes_ = 0;

#ifdef _WIN32
    bool have_prev_cpu_sample_ = false;
    std::uint64_t prev_proc_cpu_time_ = 0;
    std::uint64_t prev_sys_cpu_time_ = 0;
#endif
};

class DebugFrameBreakdown {
public:
    void beginFrame();
    void setBaseStats(const DebugPerfStats& base);
    void finalizeFrame();
    DebugPerfStats snapshot() const;

    void setInputMs(float v);
    void setSpeechMs(float v);
    void setTouchMs(float v);
    void setPhysicsMs(float v);
    void setAutonomousMs(float v);
    void setEyeMs(float v);
    void setSoftMeshMs(float v);
    void setRenderMs(float v);
    void setUiMs(float v);
    void setSwapMs(float v);

private:
    DebugPerfStats current_{};
    DebugPerfStats latest_{};
};

struct ScopedTimerMs {
    using Clock = std::chrono::high_resolution_clock;

    Clock::time_point start = Clock::now();
    float* out_ms = nullptr;

    explicit ScopedTimerMs(float* out);
    ~ScopedTimerMs();
};

double bytesToMiB(std::uint64_t bytes);

void drawDebugOverlay(
    const DebugPerfStats& stats,
    const AppConfig& config,
    const physics::PsdAssembly& assembly,
    const physics::PhysicsScene* physics_scene,
    const ChatUI* chat_ui,
    const LlamaChatLoader* loader,
    int display_w,
    int display_h);

} // namespace util