// main.cpp
#include "AppConfig.hpp"
#include "LlamaChatLoader.hpp"
#include "ImGuiLayer.hpp"
#include "ChatPanel.hpp"
#include "AudioEngine.hpp"
#include "SpeechPipeline.hpp"
#include "ConversationMemoryService.hpp"
#include "SaveLauncherPanel.hpp"
#include "PhysicsScene.hpp"
#include "GameRenderer.hpp"
#include "PhysicsBodyFactory.hpp"
#include "PsdAssembler.hpp"
#include "SceneConfig.hpp"
#include "SoftBodyInteractor.hpp"

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

#define STB_IMAGE_IMPLEMENTATION
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

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(launcher_width, launcher_height, launcher_title, nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    setWindowIcons(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    ImGuiLayer imgui;
    if (!imgui.initialize(window, "#version 330")) {
        std::cerr << "Failed to initialize Dear ImGui.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    SaveLauncherPanel launcher(template_config_path, saves_root);
    std::string launcher_error;
    if (!launcher.initialize(launcher_error)) {
        std::cerr << launcher_error << "\n";
    }

    std::unique_ptr<AudioEngine> audio;
    std::unique_ptr<SpeechPipeline> speech;
    std::unique_ptr<LlamaChatLoader> loader;
    std::unique_ptr<ConversationMemoryService> memory_service;
    std::unique_ptr<ChatPanel> panel;
    std::unique_ptr<physics::PhysicsScene> physics_scene;
    std::unique_ptr<render::GameRenderer> game_renderer;
    physics::PsdAssembly psd_assembly;
    std::unique_ptr<physics::SoftBodyInteractor> soft_interactor;

    AppConfig active_config;

    std::random_device rd;
    std::mt19937_64 rng(rd());
    int64_t session_id = static_cast<int64_t>(rng());

    bool game_started = false;
    bool pause_menu_open = false;

    double last_frame_time = glfwGetTime();
    double mouse_x = 0.0;
    double mouse_y = 0.0;

    std::string current_scene_id;

    bool prev_left_down = false;
    bool esc_was_down = false;
    bool r_was_down = false;

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

        clearGameScene();
        current_scene_id.clear();
        panel.reset();
        memory_service.reset();
        loader.reset();
        speech.reset();
        audio.reset();

        active_config = AppConfig{};

        launcher = SaveLauncherPanel(template_config_path, saves_root);
        std::string reset_error;
        if (!launcher.initialize(reset_error)) {
            std::cerr << reset_error << "\n";
        }

        glfwSetWindowMonitor(window, nullptr, 100, 100, launcher_width, launcher_height, 0);
        glfwSetWindowTitle(window, launcher_title);
        glfwSwapInterval(1);

        game_started = false;

        std::mt19937_64 new_rng(std::random_device{}());
        session_id = static_cast<int64_t>(new_rng());
    };

    game_renderer = std::make_unique<render::GameRenderer>();
    std::string render_error;
    if (!game_renderer->initialize(render_error)) {
        std::cerr << "Failed to initialize GameRenderer: " << render_error << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    soft_interactor = std::make_unique<physics::SoftBodyInteractor>();
    physics::SoftBodyInteractor::Config drag_cfg;
    drag_cfg.pickRadiusPx = 28.0f;
    drag_cfg.springStiffness = 2000.0f;
    drag_cfg.springDamping = 100.0f;
    drag_cfg.maxForce = (cpFloat)2e2;
    soft_interactor->setConfig(drag_cfg);

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

            if (physics_scene) {
                physics_scene->resizeBounds(display_w, display_h);
                double now = glfwGetTime();
                double dt = now - last_frame_time;
                last_frame_time = now;
                physics_scene->step(dt);
            }

            for (physics::RenderPart& part : psd_assembly.parts) {
                if (part.kind == physics::PartKind::Soft) {
                    game_renderer->updateSoftRenderMesh(part);
                }
            }

            game_renderer->beginFrame(display_w, display_h);
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
                SaveEntry launched_save = launcher.selectedSave();

                std::string opened_error;
                if (!SaveManager::markSaveOpenedNow(launched_save, opened_error)) {
                    std::cerr << "Failed to update last opened time: " << opened_error << "\n";
                }

                std::string reload_error;
                if (!SaveManager::loadSaveConfig(launched_save, active_config, reload_error)) {
                    std::cerr << "Failed to reload save config after open timestamp update: " << reload_error << "\n";
                    active_config = launcher.selectedConfig();
                }

                GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* video_mode = primary_monitor ? glfwGetVideoMode(primary_monitor) : nullptr;

                glfwSetWindowTitle(window, launcher.selectedSave().safe_name.c_str());

                if (active_config.window.fullscreen && primary_monitor && video_mode) {
                    glfwSetWindowMonitor(
                        window,
                        primary_monitor,
                        0,
                        0,
                        video_mode->width,
                        video_mode->height,
                        video_mode->refreshRate
                    );
                } else {
                    glfwSetWindowMonitor(
                        window,
                        nullptr,
                        100,
                        100,
                        active_config.window.width,
                        active_config.window.height,
                        0
                    );
                }

                glfwSwapInterval(active_config.window.vsync ? 1 : 0);

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

                physics_scene = std::make_unique<physics::PhysicsScene>();
                int fbw = 0;
                int fbh = 0;
                glfwGetFramebufferSize(window, &fbw, &fbh);

                physics::PhysicsSceneConfig scene_config;
                if (!physics_scene->initialize(fbw, fbh, scene_config)) {
                    std::cerr << "Failed to initialize physics scene.\n";
                }

                std::string assembly_error;
                current_scene_id = "robot_idle";
                if (!loadSceneById(current_scene_id)) {
                    std::cerr << "Failed to assemble PSD character.\n";
                }

                pause_menu_open = false;
                game_started = true;
            }
        } else {
            if (!panel && loader && loader->isReady()) {
                panel = std::make_unique<ChatPanel>(
                    *loader->getChat(),
                    *speech,
                    active_config,
                    memory_service.get(),
                    soft_interactor.get(),
                    session_id
                );
            }

            if (panel) {
                panel->draw();
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
                    resetToLauncher();
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
    }

    panel.reset();
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