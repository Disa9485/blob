// PsdAssembler.hpp
#pragma once

#include "PhysicsRuntimeTypes.hpp"
#include "SceneConfig.hpp"

#include <chipmunk/chipmunk.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace render {
class GameRenderer;
}

namespace physics {

struct JointAnimationRuntime {
    bool enabled = false;
    bool parentIsScene = false;

    cpBody* childAngleBody = nullptr;
    cpBody* parentAngleBody = nullptr;
    cpConstraint* motor = nullptr;

    float minDeg = 0.0f;
    float maxDeg = 0.0f;

    struct Target {
        float targetDeg = 0.0f;
        float moveDurationMs = 0.0f;
        float holdDurationMs = 0.0f;
    };

    std::vector<Target> targets;
    std::size_t currentIndex = 0;

    enum class Phase {
        Moving,
        Holding
    };

    Phase phase = Phase::Moving;

    float phaseElapsedMs = 0.0f;
    float moveStartDeg = 0.0f;
    float moveTargetDeg = 0.0f;

    // For scene-parent joints, store bind/reference angle.
    float sceneBaseAngleDeg = 0.0f;
};

struct JointTranslationRuntime {

    bool enabled = false;

    cpBody* body = nullptr;
    cpBody* staticBody = nullptr;

    cpConstraint* pivot = nullptr;
    cpVect pivotOffsetLocal = cpvzero;

    struct Target {
        Vec2 targetPx;
        float moveDurationMs;
        float holdDurationMs;
    };

    std::vector<Target> targets;

    size_t currentIndex = 0;

    enum class Phase {
        Moving,
        Holding
    };

    Phase phase = Phase::Moving;

    float phaseElapsedMs = 0.0f;

    cpVect baseWorld;
    cpVect moveStart;
    cpVect moveTarget;
};

struct JointPair {
    cpConstraint* pivot = nullptr;
    cpConstraint* framePivot = nullptr;
    cpConstraint* limit = nullptr;
    cpConstraint* rotSpring = nullptr;

    JointAnimationRuntime animation;
    JointTranslationRuntime translation;
};

struct DialogueAnchorRuntime {
    bool valid = false;
    std::string partName;
    RenderPart* part = nullptr;

    // The original PSD/canvas point from config.
    Vec2 attachCanvas = { 0.0f, 0.0f };

    // For rigid parts: anchor in body-local world units.
    cpVect rigidLocal = cpvzero;

    // For soft parts: nearest vertex and local offset relative to that vertex.
    int softVertexIndex = -1;
    cpVect softVertexLocalOffset = cpvzero;
};

struct PsdAssembly {
    std::string sceneId;
    std::string psdPath;
    std::string configPath;

    float sceneScale = 1.0f;

    std::vector<RenderPart> parts;
    std::unordered_map<std::string, RenderPart*> partsByName;
    std::unordered_map<std::string, RenderItem> renderRegistry;
    std::vector<RenderItem> renderItems;

    std::vector<JointPair> joints;
    std::unique_ptr<CollisionRules> collisionRules;

    DialogueAnchorRuntime dialogueAnchor;

    bool setItemVisible(const std::string& name, bool visible);
    bool isItemVisible(const std::string& name) const;
    void setOnlyVisible(const std::vector<std::string>& namesToHideShow, const std::string& visibleName);
};

struct SceneFiles {
    std::string psdPath;
    std::string configPath;
    std::string baseDir;
};

class PsdAssembler {
public:
    static bool resolveSceneFiles(
        const std::string& sceneId,
        SceneFiles& out,
        std::string& error
    );

    static void updateJointAnimations(PsdAssembly& assembly, double dt);

    static bool buildScene(
        cpSpace* space,
        render::GameRenderer& renderer,
        const std::string& sceneId,
        int sceneWidth,
        int sceneHeight,
        PsdAssembly& out,
        std::string& error
    );

    static bool rebuildScene(
        cpSpace* space,
        render::GameRenderer& renderer,
        const std::string& sceneId,
        int sceneWidth,
        int sceneHeight,
        PsdAssembly& out,
        std::string& error
    );

    static void destroyAssembly(
        cpSpace* space,
        render::GameRenderer& renderer,
        PsdAssembly& assembly
    );

    static bool getDialogueAnchorNormalized(
        const PsdAssembly& assembly,
        int sceneWidth,
        int sceneHeight,
        Vec2& outNormalized
    );
};

} // namespace physics