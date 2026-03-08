// GameRenderer.hpp
#pragma once

#include "PhysicsRuntimeTypes.hpp"
#include "PhysicsTypes.hpp"

#include <glad/glad.h>

#include <string>
#include <vector>

namespace render {

class GameRenderer {
public:
    GameRenderer() = default;
    ~GameRenderer();

    GameRenderer(const GameRenderer&) = delete;
    GameRenderer& operator=(const GameRenderer&) = delete;

    bool initialize(std::string& error);
    void shutdown();

    void beginFrame(int framebufferWidth, int framebufferHeight);
    void renderParts(const std::vector<physics::RenderItem>& items);
    void endFrame();

    GLuint uploadTextureRGBA(const std::uint8_t* rgba, int w, int h);

    bool initializeSoftRenderMesh(physics::RenderPart& part, std::string& error);
    void updateSoftRenderMesh(physics::RenderPart& part);
    void destroySoftRenderMesh(physics::RenderPart& part);

private:
    GLuint compileShader(GLenum type, const char* src, std::string& error);
    GLuint createProgram(const char* vsSrc, const char* fsSrc, std::string& error);
    void updateQuadVBO_Rotated(
        float cx, float cy,
        float hw, float hh,
        float angleRad,
        float u0, float v0, float u1, float v1
    );

    GLuint program_ = 0;
    GLint uScreenLoc_ = -1;
    GLint uTexLoc_ = -1;

    GLuint quadVao_ = 0;
    GLuint quadVbo_ = 0;
    GLuint quadEbo_ = 0;

    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
};

} // namespace render