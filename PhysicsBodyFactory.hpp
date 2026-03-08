// PhysicsBodyFactory.hpp
#pragma once

#include "PhysicsTypes.hpp"

namespace physics {

class PhysicsBodyFactory {
public:
    static RigidBodyData createRigidBodyFromImage(
        cpSpace* space,
        const std::string& name,
        const ImageRGBA& image,
        Vec2 canvasCenter,
        Vec2 worldPos,
        float scale,
        const RigidBuildParams& params
    );

    static SoftBodyData createSoftBodyFromAlpha(
        cpSpace* space,
        const std::string& name,
        const ImageRGBA& image,
        Vec2 canvasCenter,
        Vec2 worldPos,
        const SoftBuildParams& params,
        const SoftPinnedBuildParams* pinnedParams = nullptr
    );

    static SoftBodyData createSoftBodyFromMesh(
        cpSpace* space,
        const std::string& name,
        const MeshData& mesh,
        Vec2 canvasCenter,
        Vec2 worldPos,
        const SoftBuildParams& params,
        const SoftPinnedBuildParams* pinnedParams = nullptr
    );

    static void destroyRigidBody(cpSpace* space, RigidBodyData& data);
    static void destroySoftBody(cpSpace* space, SoftBodyData& data);

private:
    static OpaqueBounds computeOpaqueBounds(const ImageRGBA& image, std::uint8_t alphaThreshold);
    static std::uint8_t alphaAt(const ImageRGBA& image, int x, int y);
    static Vec2 computeOpaqueCentroid(const ImageRGBA& image, std::uint8_t alphaThreshold);

    static std::vector<Vec2> extractPerimeterPoints(
        const ImageRGBA& image,
        std::uint8_t alphaThreshold,
        float spacingPx
    );

    static std::vector<Vec2> extractInteriorGridPoints(
        const ImageRGBA& image,
        std::uint8_t alphaThreshold,
        float spacingPx
    );

    static Vec2 canvasToWorld(
        Vec2 canvasPt,
        Vec2 canvasCenter,
        Vec2 worldPos,
        float scale
    );

    static cpVect canvasToWorldCp(
        Vec2 canvasPt,
        Vec2 canvasCenter,
        Vec2 worldPos,
        float scale
    );

    static void addConstraint(cpSpace* space, SoftBody& body, cpConstraint* c);
};

} // namespace physics