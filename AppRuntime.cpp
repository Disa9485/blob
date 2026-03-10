// AppRuntime.cpp
#include "AppRuntime.hpp"

#include <glad/glad.h>
#include <imgui.h>

#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

AppRuntime::AppRuntime(std::string template_config_path, std::string saves_root)
    : template_config_path_(std::move(template_config_path))
    , saves_root_(std::move(saves_root))
    , launcher_(template_config_path_, saves_root_)
    , session_rng_(std::random_device{}())
    , session_id_(static_cast<int64_t>(session_rng_()))
    , autonomous_rng_(std::random_device{}())
{
}

AppRuntime::~AppRuntime() {
    shutdown();
}

bool AppRuntime::initialize(std::string& error) {
    if (!initializeLauncherWindow(error)) {
        return false;
    }

    if (!initializeLauncher(error)) {
        return false;
    }

    if (!initializeRoomManager(error)) {
        return false;
    }

    createSoftInteractor();
    last_frame_time_ = glfwGetTime();
    return true;
}

void AppRuntime::shutdown() {
    requestStopLongRunningTasks();

    chat_ui_.reset();
    memory_service_.reset();
    loader_.reset();
    speech_.reset();
    audio_.reset();

    clearGameScene();
    game_renderer_.reset();

    imgui_.shutdown();

    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

bool AppRuntime::initializeLauncherWindow(std::string& error) {
    util::WindowCreateParams params;
    params.width = kLauncherWidth;
    params.height = kLauncherHeight;
    params.title = kLauncherTitle;
    params.fullscreen = false;
    params.resizable = false;
    params.vsync = true;
    params.msaa_samples = 0;

    return util::recreateWindowAndGraphics(window_, imgui_, game_renderer_, params, error);
}

bool AppRuntime::initializeLauncher(std::string& error) {
    return launcher_.initialize(error);
}

bool AppRuntime::initializeRoomManager(std::string& error) {
    room_manager_ = std::make_unique<rooms::RoomManager>();
    if (!room_manager_->initialize("assets/rooms", error)) {
        return false;
    }
    return true;
}

void AppRuntime::createSoftInteractor() {
    soft_interactor_ = std::make_unique<physics::SoftBodyInteractor>();
    physics::SoftBodyInteractor::Config drag_cfg;
    drag_cfg.pickRadiusPx = 28.0f;
    drag_cfg.springStiffness = 2000.0f;
    drag_cfg.springDamping = 100.0f;
    drag_cfg.maxForce = (cpFloat)2e2;
    soft_interactor_->setConfig(drag_cfg);
}

bool AppRuntime::recreateWindowCenteredOnCurrent(
    const util::WindowCreateParams& params_in,
    std::string& error)
{
    util::WindowCreateParams params = params_in;

    int old_x = 0;
    int old_y = 0;
    int old_w = 0;
    int old_h = 0;

    if (window_) {
        glfwGetWindowPos(window_, &old_x, &old_y);
        glfwGetWindowSize(window_, &old_w, &old_h);
    }

    const int center_x = old_x + old_w / 2;
    const int center_y = old_y + old_h / 2;

    params.use_position = !params.fullscreen;
    if (!params.fullscreen) {
        params.pos_x = center_x - params.width / 2;
        params.pos_y = center_y - params.height / 2;
    }

    return util::recreateWindowAndGraphics(window_, imgui_, game_renderer_, params, error);
}

bool AppRuntime::recreateLauncherWindowCentered(std::string& error) {
    util::WindowCreateParams params;
    params.width = kLauncherWidth;
    params.height = kLauncherHeight;
    params.title = kLauncherTitle;
    params.fullscreen = false;
    params.resizable = false;
    params.vsync = true;
    params.msaa_samples = 0;
    return recreateWindowCenteredOnCurrent(params, error);
}

bool AppRuntime::recreateGameWindowCentered(const AppConfig& config, const char* title, std::string& error) {
    util::WindowCreateParams params;
    params.width = config.window.width;
    params.height = config.window.height;
    params.title = title;
    params.fullscreen = config.window.fullscreen;
    params.resizable = false;
    params.vsync = config.window.vsync;
    params.msaa_samples = config.window.anti_aliasing_samples;
    return recreateWindowCenteredOnCurrent(params, error);
}

void AppRuntime::requestStopLongRunningTasks() {
    cancellation_.stop_requested.store(true);

    if (chat_ui_) {
        chat_ui_->requestStopGeneration();
    }

    if (loader_) {
        loader_->requestStop();
    }
}

bool AppRuntime::tryAutonomousSpeech(const std::string& prompt, float chance) {
    if (!chat_ui_ || chat_ui_->isGenerating()) {
        return false;
    }

    if (chance <= 0.0f) {
        return false;
    }

    if (autonomous_roll_(autonomous_rng_) <= chance) {
        chat_ui_->startAutonomousGeneration(prompt);
        return true;
    }

    return false;
}

void AppRuntime::clearGameScene() {
    desired_eye_state_ = "loading";
    applied_eye_state_.clear();
    eye_controller_.reset();

    if (soft_interactor_ && physics_scene_) {
        soft_interactor_->shutdown(physics_scene_->space());
    }
    soft_interactor_.reset();

    if (physics_scene_ && game_renderer_) {
        physics::PsdAssembler::destroyAssembly(
            physics_scene_->space(),
            *game_renderer_,
            psd_assembly_
        );
    }

    physics_scene_.reset();
    psd_assembly_ = physics::PsdAssembly{};
}

void AppRuntime::syncEyeState() {
    if (!eye_controller_) {
        return;
    }

    if (applied_eye_state_ != desired_eye_state_) {
        if (eye_controller_->pickEye(desired_eye_state_)) {
            applied_eye_state_ = desired_eye_state_;
        }
    }
}

bool AppRuntime::loadSceneById(const std::string& sceneId) {
    if (!physics_scene_ || !game_renderer_) {
        return false;
    }

    physics::SceneFiles files;
    std::string files_error;
    if (!physics::PsdAssembler::resolveSceneFiles(sceneId, files, files_error)) {
        std::cerr << "Failed to resolve scene files for '" << sceneId << "': " << files_error << "\n";
        return false;
    }

    physics::SceneConfig scene_config_data;
    std::string scene_config_error;
    if (!physics::SceneConfig::loadFromFile(files.configPath, scene_config_data, scene_config_error)) {
        std::cerr << "Failed to load scene config for '" << sceneId << "': " << scene_config_error << "\n";
        return false;
    }

    physics_scene_->setWallsEnabled(scene_config_data.scene.walls);

    if (soft_interactor_) {
        soft_interactor_->shutdown(physics_scene_->space());
    }

    physics::PsdAssembler::destroyAssembly(
        physics_scene_->space(),
        *game_renderer_,
        psd_assembly_
    );

    int fbw = 0;
    int fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);

    std::string assembly_error;
    if (!physics::PsdAssembler::buildScene(
            physics_scene_->space(),
            *game_renderer_,
            sceneId,
            fbw,
            fbh,
            psd_assembly_,
            assembly_error)) {
        std::cerr << "Failed to build scene '" << sceneId << "': " << assembly_error << "\n";
        return false;
    }

    eye_controller_ = std::make_unique<physics::EyeController>();
    if (!eye_controller_->initialize(&psd_assembly_)) {
        std::cerr << "Failed to initialize eye controller.\n";
    } else {
        desired_eye_state_ = "loading";
        applied_eye_state_.clear();
        syncEyeState();
    }

    return true;
}

bool AppRuntime::persistActiveConfig() {
    if (!active_save_.has_value()) {
        return false;
    }

    std::string error;
    if (!SaveManager::saveSaveConfig(*active_save_, active_config_, error)) {
        std::cerr << "Failed to save active config: " << error << "\n";
        return false;
    }

    return true;
}

bool AppRuntime::setRoomById(const std::string& roomId, bool persist) {
    if (!room_manager_ || !game_renderer_) {
        return false;
    }

    std::string error;
    if (!room_manager_->setCurrentRoom(roomId, error)) {
        std::cerr << "Failed to set room '" << roomId << "': " << error << "\n";
        return false;
    }

    game_renderer_->setBackgroundTexture(room_manager_->currentTexture());
    active_config_.current_room = roomId;

    if (persist) {
        persistActiveConfig();
    }

    return true;
}

void AppRuntime::resetToLauncher() {
    pause_menu_open_ = false;

    requestStopLongRunningTasks();

    chat_ui_.reset();
    memory_service_.reset();
    loader_.reset();
    speech_.reset();
    audio_.reset();

    clearGameScene();
    current_scene_id_.clear();
    active_save_.reset();

    if (game_renderer_) {
        game_renderer_->clearBackgroundTexture();
    }

    active_config_ = AppConfig{};
    pending_launch_save_.reset();
    pending_launch_config_ = AppConfig{};
    request_launch_save_ = false;

    was_generating_last_frame_ = false;
    boot_autonomous_roll_done_ = false;
    idle_talk_accumulator_seconds_ = 0.0;
    last_processed_touch_event_serial_ = 0;

    launcher_ = SaveLauncher(template_config_path_, saves_root_);
    std::string reset_error;
    if (!launcher_.initialize(reset_error)) {
        std::cerr << reset_error << "\n";
    }

    {
        std::string recreate_error;
        if (!recreateLauncherWindowCentered(recreate_error)) {
            std::cerr << recreate_error << "\n";
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
            return;
        }
    }

    std::string room_error;
    if (!initializeRoomManager(room_error)) {
        std::cerr << "Failed to initialize RoomManager: " << room_error << "\n";
    }

    game_started_ = false;
    session_id_ = static_cast<int64_t>(session_rng_());
    last_frame_time_ = glfwGetTime();
}

bool AppRuntime::launchPendingSave() {
    if (!request_launch_save_ || !pending_launch_save_.has_value()) {
        return false;
    }

    request_launch_save_ = false;

    SaveEntry launched_save = *pending_launch_save_;
    pending_launch_save_.reset();

    active_save_ = launched_save;
    active_config_ = pending_launch_config_;

    std::string opened_error;
    if (!SaveManager::markSaveOpenedNow(launched_save, opened_error)) {
        std::cerr << "Failed to update last opened time: " << opened_error << "\n";
    }

    std::string reload_error;
    if (!SaveManager::loadSaveConfig(launched_save, active_config_, reload_error)) {
        std::cerr << "Failed to reload save config after open timestamp update: " << reload_error << "\n";
    }

    {
        std::string recreate_error;
        if (!recreateGameWindowCentered(active_config_, active_save_->safe_name.c_str(), recreate_error)) {
            std::cerr << recreate_error << "\n";
            return false;
        }

        std::string room_error;
        if (!initializeRoomManager(room_error)) {
            std::cerr << "Failed to initialize RoomManager: " << room_error << "\n";
        }
    }

    audio_ = std::make_unique<AudioEngine>();
    std::string audio_error;
    if (!audio_->initialize(audio_error)) {
        std::cerr << audio_error << "\n";
    }

    speech_ = std::make_unique<SpeechPipeline>();
    std::string speech_error;
    if (active_config_.tts.enabled) {
        if (!speech_->initialize(*audio_, active_config_.tts, "espeak-ng-data", speech_error)) {
            std::cerr << speech_error << "\n";
        }
    }

    loader_ = std::make_unique<LlamaChatLoader>();
    loader_->setCancellation(&cancellation_);
    loader_->start(
        active_config_.model_path,
        active_config_.buildStaticSystemPrompt(),
        active_config_.llm_options
    );

    memory_service_ = std::make_unique<ConversationMemoryService>();
    std::string memory_error;
    if (!memory_service_->initialize(active_config_.memory, memory_error)) {
        std::cerr << "Memory initialization failed: " << memory_error << "\n";
    }

    createSoftInteractor();

    physics_scene_ = std::make_unique<physics::PhysicsScene>();
    int fbw = 0;
    int fbh = 0;
    glfwGetFramebufferSize(window_, &fbw, &fbh);

    physics::PhysicsSceneConfig scene_config;
    if (!physics_scene_->initialize(fbw, fbh, scene_config)) {
        std::cerr << "Failed to initialize physics scene.\n";
    }

    game_renderer_->setLightingConfig(active_config_.lighting);

    current_scene_id_ = "robot_idle";
    if (!loadSceneById(current_scene_id_)) {
        std::cerr << "Failed to assemble PSD character.\n";
    }

    if (room_manager_) {
        const std::vector<std::string> ids = room_manager_->roomIds();
        if (!ids.empty()) {
            if (!setRoomById(active_config_.current_room, false)) {
                std::cerr << "Failed to set configured room '" << active_config_.current_room
                          << "', falling back to 'lab'.\n";

                if (setRoomById("lab", false)) {
                    active_config_.current_room = "lab";
                    persistActiveConfig();
                } else {
                    std::cerr << "Failed to set fallback room 'lab'.\n";
                    game_renderer_->clearBackgroundTexture();
                }
            }
        } else {
            game_renderer_->clearBackgroundTexture();
        }
    }

    last_frame_time_ = glfwGetTime();
    pause_menu_open_ = false;
    game_started_ = true;

    was_generating_last_frame_ = false;
    boot_autonomous_roll_done_ = false;
    idle_talk_accumulator_seconds_ = 0.0;
    last_processed_touch_event_serial_ = 0;

    return true;
}

void AppRuntime::updatePerFrameInput() {
    const bool esc_down = glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    const bool esc_pressed = esc_down && !esc_was_down_;
    esc_was_down_ = esc_down;

    const bool r_down = glfwGetKey(window_, GLFW_KEY_R) == GLFW_PRESS;
    const bool r_pressed = r_down && !r_was_down_;
    r_was_down_ = r_down;

    if (game_started_ && !pause_menu_open_ && r_pressed && !current_scene_id_.empty()) {
        if (!loadSceneById(current_scene_id_)) {
            std::cerr << "Failed to reload current scene: " << current_scene_id_ << "\n";
        }
    }

    if (game_started_ && esc_pressed) {
        pause_menu_open_ = !pause_menu_open_;
    }
}

void AppRuntime::updateSpeechTiming() {
    if (game_started_ && speech_ && !pause_menu_open_) {
        float ms = 0.0f;
        {
            util::ScopedTimerMs timer(&ms);
            speech_->update();
        }
        debug_frame_breakdown_.setSpeechMs(ms);
    }
}

void AppRuntime::updateGameRenderingAndSimulation(int display_w, int display_h) {
    glViewport(0, 0, display_w, display_h);

    if (game_started_ && game_renderer_) {
        float input_ms = 0.0f;
        {
            util::ScopedTimerMs timer(&input_ms);

            double cursor_x = 0.0;
            double cursor_y = 0.0;
            glfwGetCursorPos(window_, &cursor_x, &cursor_y);
            mouse_x_ = cursor_x;
            mouse_y_ = cursor_y;

            int win_w = 0;
            int win_h = 0;
            glfwGetWindowSize(window_, &win_w, &win_h);

            const bool left_down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool left_pressed = left_down && !prev_left_down_;
            const bool left_released = !left_down && prev_left_down_;
            prev_left_down_ = left_down;

            const float mx = (win_w > 0) ? (float(mouse_x_) * float(display_w) / float(win_w)) : 0.0f;
            const float my = (win_h > 0) ? (float(mouse_y_) * float(display_h) / float(win_h)) : 0.0f;

            const ImGuiIO& io = ImGui::GetIO();
            const bool allow_scene_mouse =
                game_started_ &&
                !pause_menu_open_ &&
                !io.WantCaptureMouse &&
                physics_scene_ &&
                soft_interactor_;

            if (allow_scene_mouse) {
                if (left_pressed) {
                    soft_interactor_->beginDrag(*physics_scene_, psd_assembly_, mx, my);
                } else if (left_down) {
                    soft_interactor_->updateDrag(*physics_scene_, mx, my);
                } else if (left_released) {
                    soft_interactor_->endDrag(physics_scene_->space());
                }
            } else if (soft_interactor_ && physics_scene_ && soft_interactor_->isDragging()) {
                soft_interactor_->endDrag(physics_scene_->space());
            }
        }
        debug_frame_breakdown_.setInputMs(input_ms);

        float touch_ms = 0.0f;
        {
            util::ScopedTimerMs timer(&touch_ms);

            if (game_started_ && chat_ui_ && soft_interactor_) {
                const std::uint64_t touch_event_serial = soft_interactor_->touchEventSerial();
                if (touch_event_serial != last_processed_touch_event_serial_) {
                    last_processed_touch_event_serial_ = touch_event_serial;

                    const float chance = active_config_.autonomous_speech.talk_on_touch_chance;
                    if (!chat_ui_->isGenerating() &&
                        chance > 0.0f &&
                        autonomous_roll_(autonomous_rng_) <= chance) {
                        chat_ui_->startAutonomousGenerationAndConsumeTouches(
                            "[" + active_config_.userName() + " is grabbing you, make a comment about it]"
                        );
                    }
                }
            }
        }
        debug_frame_breakdown_.setTouchMs(touch_ms);

        if (physics_scene_) {
            physics_scene_->resizeBounds(display_w, display_h);
            const double now = glfwGetTime();
            const double dt = now - last_frame_time_;
            last_frame_time_ = now;

            debug_perf_sampler_.update(dt);

            float physics_ms = 0.0f;
            {
                util::ScopedTimerMs timer(&physics_ms);
                physics_scene_->step(dt);
            }
            debug_frame_breakdown_.setPhysicsMs(physics_ms);

            float autonomous_ms = 0.0f;
            {
                util::ScopedTimerMs timer(&autonomous_ms);

                if (game_started_ && chat_ui_ && !pause_menu_open_) {
                    idle_talk_accumulator_seconds_ += dt;

                    while (idle_talk_accumulator_seconds_ >= 60.0) {
                        idle_talk_accumulator_seconds_ -= 60.0;

                        tryAutonomousSpeech(
                            "[" + active_config_.userName() + " is in front of you but isn't talking]",
                            active_config_.autonomous_speech.talk_per_minute_chance
                        );
                    }
                }
            }
            debug_frame_breakdown_.setAutonomousMs(autonomous_ms);

            float eye_ms = 0.0f;
            {
                util::ScopedTimerMs timer(&eye_ms);
                if (eye_controller_) {
                    eye_controller_->update(dt);
                }
            }
            debug_frame_breakdown_.setEyeMs(eye_ms);
        }

        float soft_mesh_ms = 0.0f;
        {
            util::ScopedTimerMs timer(&soft_mesh_ms);
            for (physics::RenderPart& part : psd_assembly_.parts) {
                if (part.kind == physics::PartKind::Soft) {
                    game_renderer_->updateSoftRenderMesh(part);
                }
            }
        }
        debug_frame_breakdown_.setSoftMeshMs(soft_mesh_ms);

        float render_ms = 0.0f;
        {
            util::ScopedTimerMs timer(&render_ms);
            game_renderer_->beginFrame(display_w, display_h);
            game_renderer_->renderBackground();
            game_renderer_->renderParts(psd_assembly_.renderItems);
            game_renderer_->endFrame();
        }
        debug_frame_breakdown_.setRenderMs(render_ms);
    } else {
        float render_ms = 0.0f;
        {
            util::ScopedTimerMs timer(&render_ms);
            glClearColor(20.0f / 255.0f, 22.0f / 255.0f, 26.0f / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        debug_frame_breakdown_.setRenderMs(render_ms);
    }
}

void AppRuntime::drawUi(int display_w, int display_h) {
    float ui_ms = 0.0f;
    {
        util::ScopedTimerMs timer(&ui_ms);

        imgui_.beginFrame();

        if (!game_started_) {
            launcher_.draw();

            if (launcher_.hasSelectedSave()) {
                pending_launch_save_ = launcher_.selectedSave();

                std::string reload_error;
                if (!SaveManager::loadSaveConfig(*pending_launch_save_, pending_launch_config_, reload_error)) {
                    std::cerr << "Failed to load selected save config: " << reload_error << "\n";
                } else {
                    request_launch_save_ = true;
                }
            }
        } else {
            if (!chat_ui_ && loader_ && loader_->isReady()) {
                chat_ui_ = std::make_unique<ChatUI>(
                    *loader_->getChat(),
                    *speech_,
                    active_config_,
                    memory_service_.get(),
                    soft_interactor_.get(),
                    session_id_
                );
                chat_ui_->setCancellation(&cancellation_);
                loader_->getChat()->setCancellation(&cancellation_);

                was_generating_last_frame_ = false;
                idle_talk_accumulator_seconds_ = 0.0;
                last_processed_touch_event_serial_ = soft_interactor_
                    ? soft_interactor_->touchEventSerial()
                    : 0;
            }

            const bool llm_loading = loader_ && !loader_->isReady() && !loader_->hasFailed();
            const bool llm_generating = chat_ui_ && chat_ui_->isGenerating();

            desired_eye_state_ = (llm_loading || llm_generating) ? "loading" : "happy";
            syncEyeState();

            if (chat_ui_) {
                int fbw = 0;
                int fbh = 0;
                glfwGetFramebufferSize(window_, &fbw, &fbh);

                physics::Vec2 normalizedAnchor;
                if (physics::PsdAssembler::getDialogueAnchorNormalized(
                        psd_assembly_,
                        fbw,
                        fbh,
                        normalizedAnchor)) {
                    chat_ui_->setSpeakerAnchorNormalized(ImVec2(normalizedAnchor.x, normalizedAnchor.y));
                }

                chat_ui_->draw();
            } else if (loader_) {
                const float elapsed = loader_->elapsedSeconds();

                ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(200, 80), ImGuiCond_Always);

                ImGui::Begin("Startup", nullptr,
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoCollapse);

                if (loader_->isLoading()) {
                    const int dots = static_cast<int>(elapsed * 2.0f) % 4;
                    const char* anim[] = { "", ".", "..", "..." };
                    ImGui::TextWrapped("%s%s", loader_->status().c_str(), anim[dots]);
                    ImGui::Spacing();
                    ImGui::Text("Elapsed: %.1f seconds", elapsed);
                } else if (loader_->hasFailed()) {
                    ImGui::Text("Model load failed.");
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", loader_->errorMessage().c_str());
                } else {
                    ImGui::Text("Preparing...");
                }

                ImGui::End();
            }

            const bool generating_now = chat_ui_ && chat_ui_->isGenerating();

            if (was_generating_last_frame_ && !generating_now) {
                idle_talk_accumulator_seconds_ = 0.0;

                if (soft_interactor_) {
                    last_processed_touch_event_serial_ = soft_interactor_->touchEventSerial();
                } else {
                    last_processed_touch_event_serial_ = 0;
                }
            }

            was_generating_last_frame_ = generating_now;

            if (game_started_ && chat_ui_ && !boot_autonomous_roll_done_) {
                boot_autonomous_roll_done_ = true;
                tryAutonomousSpeech(
                    "[" + active_config_.userName() + " just entered your view]",
                    active_config_.autonomous_speech.talk_on_boot_chance
                );
            }

            if (pause_menu_open_) {
                ImGui::OpenPopup("Paused");
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(
                ImVec2(
                    viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                    viewport->WorkPos.y + viewport->WorkSize.y * 0.5f
                ),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.5f)
            );
            ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_Appearing);

            bool keep_pause_open = pause_menu_open_;
            if (ImGui::BeginPopupModal("Paused", &keep_pause_open,
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove)) {

                if (ImGui::Button("Exit to Desktop", ImVec2(-1.0f, 0.0f))) {
                    requestStopLongRunningTasks();
                    glfwSetWindowShouldClose(window_, GLFW_TRUE);
                }

                if (ImGui::Button("Exit to Launcher", ImVec2(-1.0f, 0.0f))) {
                    requestStopLongRunningTasks();
                    ImGui::CloseCurrentPopup();
                    request_reset_to_launcher_ = true;
                }

                ImGui::EndPopup();
            } else {
                pause_menu_open_ = false;
            }

            if (!keep_pause_open) {
                pause_menu_open_ = false;
            }

            if (active_config_.window.debug_mode) {
                util::drawDebugOverlay(
                    debug_frame_breakdown_.snapshot(),
                    active_config_,
                    psd_assembly_,
                    physics_scene_.get(),
                    chat_ui_.get(),
                    loader_.get(),
                    display_w,
                    display_h
                );
            }
        }

        imgui_.endFrame();
    }

    debug_frame_breakdown_.setUiMs(ui_ms);
}

void AppRuntime::finishFrame() {
    float swap_ms = 0.0f;
    {
        util::ScopedTimerMs timer(&swap_ms);
        glfwSwapBuffers(window_);
    }
    debug_frame_breakdown_.setSwapMs(swap_ms);

    debug_frame_breakdown_.setBaseStats(debug_perf_sampler_.snapshot());
    debug_frame_breakdown_.finalizeFrame();
}

int AppRuntime::run() {
    while (true) {
        glfwPollEvents();

        if (glfwWindowShouldClose(window_)) {
            requestStopLongRunningTasks();

            const bool loader_busy = loader_ && loader_->isLoading();
            const bool chat_busy = chat_ui_ && chat_ui_->isGenerating();

            if (loader_busy || chat_busy) {
                continue;
            }

            break;
        }

        debug_frame_breakdown_.beginFrame();
        debug_frame_breakdown_.setBaseStats(debug_perf_sampler_.snapshot());

        updatePerFrameInput();
        updateSpeechTiming();

        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window_, &display_w, &display_h);

        updateGameRenderingAndSimulation(display_w, display_h);
        drawUi(display_w, display_h);
        finishFrame();

        if (request_reset_to_launcher_) {
            requestStopLongRunningTasks();

            const bool loader_busy = loader_ && loader_->isLoading();
            const bool chat_busy = chat_ui_ && chat_ui_->isGenerating();

            if (loader_busy || chat_busy) {
                continue;
            }

            request_reset_to_launcher_ = false;
            resetToLauncher();
            continue;
        }

        if (request_launch_save_ && pending_launch_save_.has_value()) {
            if (!launchPendingSave()) {
                return 1;
            }
            continue;
        }
    }

    return 0;
}