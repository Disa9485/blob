// PhysicsTypes.hpp
#pragma once

#include <chipmunk/chipmunk.h>

#include <cstdint>
#include <string>
#include <vector>

namespace physics {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(float s) const { return { x * s, y * s }; }
};

float dot(const Vec2& a, const Vec2& b);
float len(const Vec2& v);

struct ImageRGBA {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct OpaqueBounds {
    int minx = 0;
    int miny = 0;
    int maxx = -1;
    int maxy = -1;
    bool valid = false;
};

struct MeshData {
    int imageWidth = 0;
    int imageHeight = 0;
    std::vector<Vec2> verticesPx;
    std::vector<Vec2> uvs;
    std::vector<std::uint32_t> indices;
};

struct SoftBody {
    std::vector<cpBody*> bodies;
    std::vector<cpShape*> shapes;
    std::vector<cpConstraint*> constraints;
    cpBody* centerBody = nullptr;

    std::vector<Vec2> modelVerts;
    std::vector<Vec2> uvs;
    std::vector<std::uint32_t> indices;
    int perimeterCount = 0;

    std::vector<std::uint8_t> pinned;
};

struct RigidBodyData {
    std::string name;

    cpBody* body = nullptr;
    cpShape* shape = nullptr;

    Vec2 anchorCanvas;

    float halfWidthWorld = 0.0f;
    float halfHeightWorld = 0.0f;

    OpaqueBounds opaqueBounds;
};

struct SoftBodyData {
    std::string name;
    Vec2 anchorCanvas;
    SoftBody body;
};

enum class PartCollisionMode {
    None,
    SharedGroup
};

struct RigidBuildParams {
    float density = 0.0005f;
    float friction = 0.1f;
    float elasticity = 0.05f;
    cpGroup collisionGroup = 0;
};

struct SoftPinnedVertexParams {
    bool enabled = false;

    bool overrideMassPerPoint = false;
    float massPerPoint = 0.0f;

    bool overrideRadius = false;
    float radius = 0.0f;

    bool overrideFriction = false;
    float friction = 0.0f;

    bool overrideElasticity = false;
    float elasticity = 0.0f;
};

struct SoftPinnedConstraintParams {
    cpFloat maxForce = (cpFloat)1e7;
    cpFloat errorBias = cpfpow(1.0f - 0.05f, 60.0f);
    cpFloat maxBias = (cpFloat)1e7;
};

struct SoftPinnedBuildParams {
    SoftPinnedVertexParams vertex;
    SoftPinnedConstraintParams constraint;
};

struct SoftBuildParams {
    float scale = 0.35f;
    float pointSpacing = 30.0f;
    float massPerPoint = 0.5f;
    float radius = 2.0f;
    float springK = 10000.0f;
    float springDamping = 20.0f;
    float friction = 0.9f;
    float elasticity = 0.2f;

    float stretchMinRatio = 0.9f;
    float stretchMaxRatio = 1.1f;
    float perimeterBendKRatio = 0.15f;
    float centerSpringKRatio = 0.4f;

    cpVect attachWorld = cpvzero;
    float attachRadiusWorld = 0.0f;

    cpGroup collisionGroup = 0;
};

} // namespace physics