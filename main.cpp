// main.cpp
#include "AppConfig.hpp"
#include "LlamaChatLoader.hpp"
#include "ImGuiLayer.hpp"
#include "ChatUI.hpp"
#include "AudioEngine.hpp"
#include "SpeechPipeline.hpp"
#include "ConversationMemoryService.hpp"
#include "SaveLauncher.hpp"
#include "PhysicsScene.hpp"
#include "GameRenderer.hpp"
#include "PhysicsBodyFactory.hpp"
#include "PsdAssembler.hpp"
#include "SceneConfig.hpp"
#include "SoftBodyInteractor.hpp"
#include "RoomManager.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <iostream>
#include <memory>
#include <string>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>
#include <optional>

#include "stb_image.h"

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

        if (!pixels)
            continue;

        GLFWimage img;
        img.width = w;
        img.height = h;
        img.pixels = pixels;

        images.push_back(img);
        pixel_data.push_back(pixels);
    }

    if (images.empty())
        return false;

    glfwSetWindowIcon(window, static_cast<int>(images.size()), images.data());

    for (auto* p : pixel_data)
        stbi_image_free(p);

    return true;
}

namespace {
struct WindowCreateParams {
    int width = 1280;
    int height = 720;
    const char* title = "Window";
    bool fullscreen = false;
    bool resizable = false;
    bool vsync = true;
    int msaa_samples = 0;
};

bool recreateWindowAndGraphics(
    GLFWwindow*& window,
    ImGuiLayer& imgui,
    std::unique_ptr<render::GameRenderer>& game_renderer,
    const WindowCreateParams& params,
    std::string& error)
{
    // Tear down renderer + ImGui before destroying the context.
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
}

int main() {
    std::cout << "Running.\n";

    const std::string template_config_path = "default_app_config.json";
    const std::string saves_root = "saves";

    constexpr int launcher_width = 720;
    constexpr int launcher_height = 640;
    constexpr const char* launcher_title = "Save Launcher";

    if (!glfwInit()) {
        return 1;
    }

    GLFWwindow* window = nullptr;

    ImGuiLayer imgui;

    std::unique_ptr<render::GameRenderer> game_renderer;

    {
        std::string init_error;
        WindowCreateParams params;
        params.width = launcher_width;
        params.height = launcher_height;
        params.title = launcher_title;
        params.fullscreen = false;
        params.resizable = false;
        params.vsync = true;
        params.msaa_samples = 0; // launcher does not need MSAA

        if (!recreateWindowAndGraphics(window, imgui, game_renderer, params, init_error)) {
            std::cerr << init_error << "\n";
            glfwTerminate();
            return 1;
        }
    }

    SaveLauncher launcher(template_config_path, saves_root);
    std::string launcher_error;
    if (!launcher.initialize(launcher_error)) {
        std::cerr << launcher_error << "\n";
    }

    std::unique_ptr<AudioEngine> audio;
    std::unique_ptr<SpeechPipeline> speech;
    std::unique_ptr<LlamaChatLoader> loader;
    std::unique_ptr<ConversationMemoryService> memory_service;
    std::unique_ptr<ChatUI> chat_ui;
    std::unique_ptr<physics::PhysicsScene> physics_scene;
    physics::PsdAssembly psd_assembly;
    std::unique_ptr<physics::SoftBodyInteractor> soft_interactor;
    std::unique_ptr<rooms::RoomManager> room_manager;
    std::optional<SaveEntry> active_save;

    AppConfig active_config;

    std::random_device rd;
    std::mt19937_64 rng(rd());
    int64_t session_id = static_cast<int64_t>(rng());

    bool game_started = false;
    bool pause_menu_open = false;

    bool request_reset_to_launcher = false;
    bool request_launch_save = false;

    std::optional<SaveEntry> pending_launch_save;
    AppConfig pending_launch_config;

    double last_frame_time = glfwGetTime();
    double mouse_x = 0.0;
    double mouse_y = 0.0;

    std::string current_scene_id;

    bool prev_left_down = false;
    bool esc_was_down = false;
    bool r_was_down = false;

    bool boot_autonomous_roll_done = false;
    double idle_talk_accumulator_seconds = 0.0;
    std::uint64_t last_processed_touch_event_serial = 0;
    bool was_generating_last_frame = false;

    std::mt19937 autonomous_rng(std::random_device{}());
    std::uniform_real_distribution<float> autonomous_roll(0.0f, 1.0f);

    auto tryAutonomousSpeech = [&](const std::string& prompt, float chance) {
        if (!chat_ui || chat_ui->isGenerating()) {
            return false;
        }

        if (chance <= 0.0f) {
            return false;
        }

        if (autonomous_roll(autonomous_rng) <= chance) {
            chat_ui->startAutonomousGeneration(prompt);
            return true;
        }

        return false;
    };

    auto clearGameScene = [&]() {
        if (soft_interactor && physics_scene) {
            soft_interactor->shutdown(physics_scene->space());
        }
        soft_interactor.reset();
        
        if (physics_scene && game_renderer) {
            physics::PsdAssembler::destroyAssembly(
                physics_scene->space(),
                *game_renderer,
                psd_assembly
            );
        }
        physics_scene.reset();
        psd_assembly = physics::PsdAssembly{};
    };

    auto loadSceneById = [&](const std::string& sceneId) -> bool {
        if (!physics_scene || !game_renderer) {
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

        physics_scene->setWallsEnabled(scene_config_data.scene.walls);

        if (soft_interactor) {
            soft_interactor->shutdown(physics_scene->space());
        }

        physics::PsdAssembler::destroyAssembly(
            physics_scene->space(),
            *game_renderer,
            psd_assembly
        );

        int fbw = 0;
        int fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);

        std::string assembly_error;
        if (!physics::PsdAssembler::buildScene(
                physics_scene->space(),
                *game_renderer,
                sceneId,
                fbw,
                fbh,
                psd_assembly,
                assembly_error)) {
            std::cerr << "Failed to build scene '" << sceneId << "': " << assembly_error << "\n";
            return false;
        }

        return true;
    };

    auto resetToLauncher = [&]() {
        pause_menu_open = false;

        chat_ui.reset();
        memory_service.reset();
        loader.reset();
        speech.reset();
        audio.reset();

        clearGameScene();
        current_scene_id.clear();
        active_save.reset();

        if (game_renderer) {
            game_renderer->clearBackgroundTexture();
        }

        active_config = AppConfig{};
        pending_launch_save.reset();
        pending_launch_config = AppConfig{};
        request_launch_save = false;

        was_generating_last_frame = false;
        boot_autonomous_roll_done = false;
        idle_talk_accumulator_seconds = 0.0;
        last_processed_touch_event_serial = 0;

        launcher = SaveLauncher(template_config_path, saves_root);
        std::string reset_error;
        if (!launcher.initialize(reset_error)) {
            std::cerr << reset_error << "\n";
        }

        {
            std::string recreate_error;
            WindowCreateParams params;
            params.width = launcher_width;
            params.height = launcher_height;
            params.title = launcher_title;
            params.fullscreen = false;
            params.resizable = false;
            params.vsync = true;
            params.msaa_samples = 0;

            if (!recreateWindowAndGraphics(window, imgui, game_renderer, params, recreate_error)) {
                std::cerr << recreate_error << "\n";
                glfwTerminate();
                std::exit(1);
            }
        }

        room_manager.reset();
        room_manager = std::make_unique<rooms::RoomManager>();
        std::string room_error;
        if (!room_manager->initialize("assets/rooms", room_error)) {
            std::cerr << "Failed to initialize RoomManager: " << room_error << "\n";
        }

        game_started = false;

        std::mt19937_64 new_rng(std::random_device{}());
        session_id = static_cast<int64_t>(new_rng());

        last_frame_time = glfwGetTime();
    };

    auto createSoftInteractor = [&]() {
        soft_interactor = std::make_unique<physics::SoftBodyInteractor>();
        physics::SoftBodyInteractor::Config drag_cfg;
        drag_cfg.pickRadiusPx = 28.0f;
        drag_cfg.springStiffness = 2000.0f;
        drag_cfg.springDamping = 100.0f;
        drag_cfg.maxForce = (cpFloat)2e2;
        soft_interactor->setConfig(drag_cfg);
    };

    auto persistActiveConfig = [&]() -> bool {
        if (!active_save.has_value()) {
            return false;
        }

        std::string error;
        if (!SaveManager::saveSaveConfig(*active_save, active_config, error)) {
            std::cerr << "Failed to save active config: " << error << "\n";
            return false;
        }

        return true;
    };

    auto setRoomById = [&](const std::string& roomId, bool persist = true) -> bool {
        if (!room_manager || !game_renderer) {
            return false;
        }

        std::string error;
        if (!room_manager->setCurrentRoom(roomId, error)) {
            std::cerr << "Failed to set room '" << roomId << "': " << error << "\n";
            return false;
        }

        game_renderer->setBackgroundTexture(room_manager->currentTexture());
        active_config.current_room = roomId;

        if (persist) {
            persistActiveConfig();
        }

        return true;
    };

    room_manager = std::make_unique<rooms::RoomManager>();
    std::string room_error;
    if (!room_manager->initialize("assets/rooms", room_error)) {
        std::cerr << "Failed to initialize RoomManager: " << room_error << "\n";
    }

    createSoftInteractor();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const bool esc_down = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        const bool esc_pressed = esc_down && !esc_was_down;
        esc_was_down = esc_down;

        const bool r_down = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
        const bool r_pressed = r_down && !r_was_down;
        r_was_down = r_down;

        if (game_started && !pause_menu_open && r_pressed && !current_scene_id.empty()) {
            if (!loadSceneById(current_scene_id)) {
                std::cerr << "Failed to reload current scene: " << current_scene_id << "\n";
            }
        }

        if (game_started && esc_pressed) {
            pause_menu_open = !pause_menu_open;
        }

        if (game_started && speech && !pause_menu_open) {
            speech->update();
        }

        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);

        glViewport(0, 0, display_w, display_h);

        if (game_started && game_renderer) {
            double cursor_x = 0.0;
            double cursor_y = 0.0;
            glfwGetCursorPos(window, &cursor_x, &cursor_y);
            mouse_x = cursor_x;
            mouse_y = cursor_y;

            int win_w = 0;
            int win_h = 0;
            glfwGetWindowSize(window, &win_w, &win_h);

            const bool left_down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool left_pressed = left_down && !prev_left_down;
            const bool left_released = !left_down && prev_left_down;
            prev_left_down = left_down;

            const float mx = (win_w > 0) ? (float(mouse_x) * float(display_w) / float(win_w)) : 0.0f;
            const float my = (win_h > 0) ? (float(mouse_y) * float(display_h) / float(win_h)) : 0.0f;

            const ImGuiIO& io = ImGui::GetIO();
            const bool allow_scene_mouse =
                game_started &&
                !pause_menu_open &&
                !io.WantCaptureMouse &&
                physics_scene &&
                soft_interactor;

            if (allow_scene_mouse) {
                if (left_pressed) {
                    soft_interactor->beginDrag(*physics_scene, psd_assembly, mx, my);
                } else if (left_down) {
                    soft_interactor->updateDrag(*physics_scene, mx, my);
                } else if (left_released) {
                    soft_interactor->endDrag(physics_scene->space());
                }
            } else if (soft_interactor && physics_scene && soft_interactor->isDragging()) {
                soft_interactor->endDrag(physics_scene->space());
            }

            // Check for autonomous touch based prompt
            if (game_started && chat_ui && soft_interactor) {
                const std::uint64_t touch_event_serial = soft_interactor->touchEventSerial();
                if (touch_event_serial != last_processed_touch_event_serial) {
                    last_processed_touch_event_serial = touch_event_serial;

                    const float chance = active_config.autonomous_speech.talk_on_touch_chance;
                    if (!chat_ui->isGenerating() &&
                        chance > 0.0f &&
                        autonomous_roll(autonomous_rng) <= chance) {
                        chat_ui->startAutonomousGenerationAndConsumeTouches(
                            "[The user is touching you, make a comment about it]"
                        );
                    }
                }
            }

            if (physics_scene) {
                physics_scene->resizeBounds(display_w, display_h);
                double now = glfwGetTime();
                double dt = now - last_frame_time;
                last_frame_time = now;
                physics_scene->step(dt);

                // Check for autonomous time based prompt
                if (game_started && chat_ui && !pause_menu_open) {
                    idle_talk_accumulator_seconds += dt;

                    while (idle_talk_accumulator_seconds >= 60.0) {
                        idle_talk_accumulator_seconds -= 60.0;

                        tryAutonomousSpeech(
                            "[The user is in front of you but isn't talking]",
                            active_config.autonomous_speech.talk_per_minute_chance
                        );
                    }
                }
            }

            for (physics::RenderPart& part : psd_assembly.parts) {
                if (part.kind == physics::PartKind::Soft) {
                    game_renderer->updateSoftRenderMesh(part);
                }
            }

            game_renderer->beginFrame(display_w, display_h);
            game_renderer->renderBackground();
            game_renderer->renderParts(psd_assembly.renderItems);
            game_renderer->endFrame();
        } else {
            glClearColor(20.0f / 255.0f, 22.0f / 255.0f, 26.0f / 255.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        imgui.beginFrame();

        if (!game_started) {
            launcher.draw();

            if (launcher.hasSelectedSave()) {
                pending_launch_save = launcher.selectedSave();

                std::string reload_error;
                if (!SaveManager::loadSaveConfig(*pending_launch_save, pending_launch_config, reload_error)) {
                    std::cerr << "Failed to load selected save config: " << reload_error << "\n";
                } else {
                    request_launch_save = true;
                }
            }
        } else {
            if (!chat_ui && loader && loader->isReady()) {
                chat_ui = std::make_unique<ChatUI>(
                    *loader->getChat(),
                    *speech,
                    active_config,
                    memory_service.get(),
                    soft_interactor.get(),
                    session_id
                );

                // Drop any autonomous state accumulated before chat was actually usable.
                was_generating_last_frame = false;
                idle_talk_accumulator_seconds = 0.0;
                last_processed_touch_event_serial = soft_interactor
                    ? soft_interactor->touchEventSerial()
                    : 0;
            }

            if (chat_ui) {
                int fbw = 0;
                int fbh = 0;
                glfwGetFramebufferSize(window, &fbw, &fbh);

                physics::Vec2 normalizedAnchor;
                if (physics::PsdAssembler::getDialogueAnchorNormalized(
                        psd_assembly,
                        fbw,
                        fbh,
                        normalizedAnchor)) {
                    chat_ui->setSpeakerAnchorNormalized(ImVec2(normalizedAnchor.x, normalizedAnchor.y));
                }

                chat_ui->draw();
            } else if (loader) {
                const float elapsed = loader->elapsedSeconds();

                ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(200, 80), ImGuiCond_Always);

                ImGui::Begin("Startup", nullptr,
                    ImGuiWindowFlags_NoTitleBar |
                    ImGuiWindowFlags_NoResize |
                    ImGuiWindowFlags_NoCollapse);

                if (loader->isLoading()) {
                    const int dots = static_cast<int>(elapsed * 2.0f) % 4;
                    const char* anim[] = { "", ".", "..", "..." };
                    ImGui::TextWrapped("%s%s", loader->status().c_str(), anim[dots]);
                    ImGui::Spacing();
                    ImGui::Text("Elapsed: %.1f seconds", elapsed);
                } else if (loader->hasFailed()) {
                    ImGui::Text("Model load failed.");
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", loader->errorMessage().c_str());
                } else {
                    ImGui::Text("Preparing...");
                }

                ImGui::End();
            }

            bool generating_now = chat_ui && chat_ui->isGenerating();

            if (was_generating_last_frame && !generating_now) {
                idle_talk_accumulator_seconds = 0.0;

                if (soft_interactor) {
                    last_processed_touch_event_serial = soft_interactor->touchEventSerial();
                } else {
                    last_processed_touch_event_serial = 0;
                }
            }

            was_generating_last_frame = generating_now;

            if (game_started && chat_ui && !boot_autonomous_roll_done) {
                boot_autonomous_roll_done = true;
                tryAutonomousSpeech("[The user just entered your view]", active_config.autonomous_speech.talk_on_boot_chance);
            }

            if (pause_menu_open) {
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

            bool keep_pause_open = pause_menu_open;
            if (ImGui::BeginPopupModal("Paused", &keep_pause_open,
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove)) {

                if (ImGui::Button("Exit to Desktop", ImVec2(-1.0f, 0.0f))) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }

                if (ImGui::Button("Exit to Launcher", ImVec2(-1.0f, 0.0f))) {
                    ImGui::CloseCurrentPopup();
                    request_reset_to_launcher = true;
                }

                // if (ImGui::Button("Close", ImVec2(-1.0f, 0.0f))) {
                //     pause_menu_open = false;
                //     ImGui::CloseCurrentPopup();
                // }

                ImGui::EndPopup();
            } else {
                pause_menu_open = false;
            }

            if (!keep_pause_open) {
                pause_menu_open = false;
            }
        }

        imgui.endFrame();
        glfwSwapBuffers(window);

        if (request_reset_to_launcher) {
            request_reset_to_launcher = false;
            resetToLauncher();
            continue;
        }

        if (request_launch_save && pending_launch_save.has_value()) {
            request_launch_save = false;

            SaveEntry launched_save = *pending_launch_save;
            pending_launch_save.reset();

            active_save = launched_save;
            active_config = pending_launch_config;

            std::string opened_error;
            if (!SaveManager::markSaveOpenedNow(launched_save, opened_error)) {
                std::cerr << "Failed to update last opened time: " << opened_error << "\n";
            }

            std::string reload_error;
            if (!SaveManager::loadSaveConfig(launched_save, active_config, reload_error)) {
                std::cerr << "Failed to reload save config after open timestamp update: " << reload_error << "\n";
            }

            {
                std::string recreate_error;
                WindowCreateParams params;
                params.width = active_config.window.width;
                params.height = active_config.window.height;
                params.title = active_save->safe_name.c_str();
                params.fullscreen = active_config.window.fullscreen;
                params.resizable = false;
                params.vsync = active_config.window.vsync;
                params.msaa_samples = active_config.window.anti_aliasing_samples;

                if (!recreateWindowAndGraphics(window, imgui, game_renderer, params, recreate_error)) {
                    std::cerr << recreate_error << "\n";
                    glfwTerminate();
                    return 1;
                }

                room_manager.reset();
                room_manager = std::make_unique<rooms::RoomManager>();
                std::string room_error;
                if (!room_manager->initialize("assets/rooms", room_error)) {
                    std::cerr << "Failed to initialize RoomManager: " << room_error << "\n";
                }
            }

            audio = std::make_unique<AudioEngine>();
            std::string audio_error;
            if (!audio->initialize(audio_error)) {
                std::cerr << audio_error << "\n";
            }

            speech = std::make_unique<SpeechPipeline>();
            std::string speech_error;
            if (active_config.tts.enabled) {
                if (!speech->initialize(*audio, active_config.tts, "espeak-ng-data", speech_error)) {
                    std::cerr << speech_error << "\n";
                }
            }

            loader = std::make_unique<LlamaChatLoader>();
            loader->start(
                active_config.model_path,
                active_config.buildStaticSystemPrompt(),
                active_config.llm_options
            );

            memory_service = std::make_unique<ConversationMemoryService>();
            std::string memory_error;
            if (!memory_service->initialize(active_config.memory, memory_error)) {
                std::cerr << "Memory initialization failed: " << memory_error << "\n";
            }

            createSoftInteractor();

            physics_scene = std::make_unique<physics::PhysicsScene>();
            int fbw = 0;
            int fbh = 0;
            glfwGetFramebufferSize(window, &fbw, &fbh);

            physics::PhysicsSceneConfig scene_config;
            if (!physics_scene->initialize(fbw, fbh, scene_config)) {
                std::cerr << "Failed to initialize physics scene.\n";
            }

            game_renderer->setLightingConfig(active_config.lighting);

            current_scene_id = "robot_idle";
            if (!loadSceneById(current_scene_id)) {
                std::cerr << "Failed to assemble PSD character.\n";
            }

            if (room_manager) {
                const std::vector<std::string> ids = room_manager->roomIds();
                if (!ids.empty()) {
                    if (!setRoomById(active_config.current_room, false)) {
                        std::cerr << "Failed to set configured room '" << active_config.current_room
                                << "', falling back to 'lab'.\n";

                        if (setRoomById("lab", false)) {
                            active_config.current_room = "lab";
                            persistActiveConfig();
                        } else {
                            std::cerr << "Failed to set fallback room 'lab'.\n";
                            game_renderer->clearBackgroundTexture();
                        }
                    }
                } else {
                    game_renderer->clearBackgroundTexture();
                }
            }

            last_frame_time = glfwGetTime();
            pause_menu_open = false;
            game_started = true;

            was_generating_last_frame = false;
            boot_autonomous_roll_done = false;
            idle_talk_accumulator_seconds = 0.0;
            last_processed_touch_event_serial = 0;

            continue;
        }
    }

    chat_ui.reset();
    memory_service.reset();
    loader.reset();
    speech.reset();
    audio.reset();

    clearGameScene();
    game_renderer.reset();

    imgui.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}