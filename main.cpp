// main.cpp
#include "AppConfig.hpp"
#include "LlamaChatLoader.hpp"
#include "ImGuiLayer.hpp"
#include "ChatPanel.hpp"
#include "AudioEngine.hpp"
#include "SpeechPipeline.hpp"
#include "ConversationMemoryService.hpp"
#include "SaveLauncherPanel.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <iostream>
#include <memory>
#include <string>
#include <cstdint>
#include <random>

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

    const std::string template_config_path = "app_config.json";
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

    AppConfig active_config;
    std::string active_config_path;

    std::random_device rd;
    std::mt19937_64 rng(rd());
    int64_t session_id = static_cast<int64_t>(rng());

    bool game_started = false;
    bool pause_menu_open = false;
    bool esc_was_down = false;

    auto resetToLauncher = [&]() {
        pause_menu_open = false;

        panel.reset();
        memory_service.reset();
        loader.reset();
        speech.reset();
        audio.reset();

        active_config = AppConfig{};
        active_config_path.clear();

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

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const bool esc_down = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        const bool esc_pressed = esc_down && !esc_was_down;
        esc_was_down = esc_down;

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
        glClear(GL_COLOR_BUFFER_BIT);

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

                active_config_path = launched_save.config_path;

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

    imgui.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}