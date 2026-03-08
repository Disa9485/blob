// PhysicsRuntimeTypes.hpp
#pragma once

#include "PhysicsTypes.hpp"

#include <glad/glad.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace physics {

enum class PartKind {
    Rigid,
    Soft
};

enum class RenderItemKind {
    Part,
    SoftOverlay
};

struct PartTexture {
    GLuint tex = 0;
    int texW = 0;
    int texH = 0;

    float halfW = 0.0f;
    float halfH = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

struct SoftRenderMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    std::size_t indexCount = 0;
    std::vector<float> vdata; // x y u v
};

struct SoftOverlay {
    std::string name;
    GLuint tex = 0;
};

struct RenderPart {
    std::string name;
    PartKind kind = PartKind::Rigid;

    cpGroup collisionGroup = 0;
    std::uint32_t collisionId = 0;

    RigidBodyData rigid;
    SoftBodyData soft;

    PartTexture render;
    SoftRenderMesh softRender;
    std::vector<SoftOverlay> overlays;

    Vec2 anchorCanvas;
};

struct RenderItem {
    RenderItemKind kind = RenderItemKind::Part;
    const RenderPart* part = nullptr;
    int overlayIndex = -1;
};

struct ShapeTag {
    std::uint32_t partId = 0;
    const char* debugName = nullptr;
};

struct CollisionRules {
    std::unordered_map<std::string, std::uint32_t> partId;
    std::unordered_set<std::uint64_t> allowedPairs;
};

} // namespace physics