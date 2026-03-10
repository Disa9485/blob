// AppRuntime.hpp
#pragma once

#include "AppConfig.hpp"
#include "AudioEngine.hpp"
#include "ChatUI.hpp"
#include "ConversationMemoryService.hpp"
#include "EyeController.hpp"
#include "GameRenderer.hpp"
#include "ImGuiLayer.hpp"
#include "LlamaChatLoader.hpp"
#include "PhysicsScene.hpp"
#include "PsdAssembler.hpp"
#include "RoomManager.hpp"
#include "SaveLauncher.hpp"
#include "SoftBodyInteractor.hpp"
#include "SpeechPipeline.hpp"
#include "Utilities.hpp"
#include "RuntimeCancellation.hpp"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>

class AppRuntime {
public:
    AppRuntime(std::string template_config_path, std::string saves_root);
    ~AppRuntime();

    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    bool initialize(std::string& error);
    int run();
    void shutdown();

private:
    bool initializeLauncherWindow(std::string& error);
    bool initializeLauncher(std::string& error);
    bool initializeRoomManager(std::string& error);
    void createSoftInteractor();

    bool recreateWindowCenteredOnCurrent(
        const util::WindowCreateParams& params,
        std::string& error);

    bool recreateLauncherWindowCentered(std::string& error);
    bool recreateGameWindowCentered(const AppConfig& config, const char* title, std::string& error);

    bool tryAutonomousSpeech(const std::string& prompt, float chance);
    void clearGameScene();
    void syncEyeState();
    bool loadSceneById(const std::string& sceneId);
    bool persistActiveConfig();
    bool setRoomById(const std::string& roomId, bool persist = true);
    void resetToLauncher();
    bool launchPendingSave();

    void updatePerFrameInput();
    void updateSpeechTiming();
    void updateGameRenderingAndSimulation(int display_w, int display_h);
    void drawUi(int display_w, int display_h);
    void finishFrame();

    void requestStopLongRunningTasks();

private:
    std::string template_config_path_;
    std::string saves_root_;

    static constexpr int kLauncherWidth = 720;
    static constexpr int kLauncherHeight = 640;
    static constexpr const char* kLauncherTitle = "Save Launcher";

    GLFWwindow* window_ = nullptr;
    ImGuiLayer imgui_;
    std::unique_ptr<render::GameRenderer> game_renderer_;

    SaveLauncher launcher_;

    std::unique_ptr<AudioEngine> audio_;
    std::unique_ptr<SpeechPipeline> speech_;
    std::unique_ptr<LlamaChatLoader> loader_;
    std::unique_ptr<ConversationMemoryService> memory_service_;
    std::unique_ptr<ChatUI> chat_ui_;
    std::unique_ptr<physics::PhysicsScene> physics_scene_;
    physics::PsdAssembly psd_assembly_;
    std::unique_ptr<physics::SoftBodyInteractor> soft_interactor_;
    std::unique_ptr<rooms::RoomManager> room_manager_;
    std::unique_ptr<physics::EyeController> eye_controller_;
    std::optional<SaveEntry> active_save_;

    AppConfig active_config_;

    std::mt19937_64 session_rng_;
    int64_t session_id_ = 0;

    bool game_started_ = false;
    bool pause_menu_open_ = false;

    bool request_reset_to_launcher_ = false;
    bool request_launch_save_ = false;

    std::optional<SaveEntry> pending_launch_save_;
    AppConfig pending_launch_config_;

    double last_frame_time_ = 0.0;
    double mouse_x_ = 0.0;
    double mouse_y_ = 0.0;

    std::string current_scene_id_;

    bool prev_left_down_ = false;
    bool esc_was_down_ = false;
    bool r_was_down_ = false;

    bool boot_autonomous_roll_done_ = false;
    double idle_talk_accumulator_seconds_ = 0.0;
    std::uint64_t last_processed_touch_event_serial_ = 0;
    bool was_generating_last_frame_ = false;

    std::mt19937 autonomous_rng_;
    std::uniform_real_distribution<float> autonomous_roll_{0.0f, 1.0f};

    std::string desired_eye_state_ = "loading";
    std::string applied_eye_state_;

    util::DebugPerfSampler debug_perf_sampler_;
    util::DebugFrameBreakdown debug_frame_breakdown_;

    RuntimeCancellation cancellation_;
};