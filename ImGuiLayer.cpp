// ImGuiLayer.cpp
#include "ImGuiLayer.hpp"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

bool ImGuiLayer::initialize(GLFWwindow* window, const char* glsl_version) {
    window_ = window;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    
    io.Fonts->AddFontFromFileTTF("assets/fonts/Figtree-Regular.ttf", 18.0f);

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;

    style.WindowPadding = ImVec2(12.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg]           = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
    colors[ImGuiCol_ChildBg]            = ImVec4(0.11f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_PopupBg]            = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);

    colors[ImGuiCol_Border]             = ImVec4(0.24f, 0.28f, 0.34f, 0.60f);
    colors[ImGuiCol_Separator]          = ImVec4(0.24f, 0.28f, 0.34f, 0.60f);

    colors[ImGuiCol_FrameBg]            = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.20f, 0.23f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.23f, 0.27f, 0.33f, 1.00f);

    colors[ImGuiCol_Button]             = ImVec4(0.23f, 0.34f, 0.47f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.29f, 0.42f, 0.58f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.18f, 0.29f, 0.41f, 1.00f);

    colors[ImGuiCol_Header]             = ImVec4(0.18f, 0.28f, 0.39f, 1.00f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.24f, 0.36f, 0.49f, 1.00f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.15f, 0.24f, 0.34f, 1.00f);

    colors[ImGuiCol_TitleBg]            = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.10f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.07f, 0.08f, 0.10f, 1.00f);

    colors[ImGuiCol_Text]               = ImVec4(0.90f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.55f, 0.60f, 0.66f, 1.00f);

    colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.10f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.24f, 0.28f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.31f, 0.36f, 0.43f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]= ImVec4(0.36f, 0.42f, 0.50f, 1.00f);

    colors[ImGuiCol_CheckMark]          = ImVec4(0.55f, 0.78f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.42f, 0.63f, 0.88f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.55f, 0.78f, 1.00f, 1.00f);

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);

    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
        ImGui_ImplGlfw_Shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

void ImGuiLayer::shutdown() {
    if (!initialized_) return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    initialized_ = false;
    window_ = nullptr;
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}