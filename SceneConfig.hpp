// SceneConfig.hpp
#pragma once

#include "PhysicsTypes.hpp"

#include <chipmunk/chipmunk.h>

#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace physics {

struct ScenePlacementConfig {
    float scale = 0.35f;
    Vec2 spawnPosition = { 0.5f, 0.5f };
    bool walls = true;
};

struct ScenePinnedConstraintConfig {
    cpFloat maxForce = (cpFloat)1e7;
    cpFloat errorBias = cpfpow(1.0f - 0.05f, 60.0f);
    cpFloat maxBias = (cpFloat)1e7;
};

struct ScenePinnedParams {
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

struct SceneSoftOverride {
    SoftBuildParams params;
    Vec2 attachCanvas;
    float attachRadiusPx = 0.0f;
    bool useMeshJson = false;
    std::string meshJsonPath;

    ScenePinnedParams pinnedParams;
    ScenePinnedConstraintConfig pinnedConstraint;
};

struct SceneCollisionRulesConfig {
    std::vector<std::pair<std::string, std::string>> allowPairs;
};

struct SceneJointConfig {
    std::string child;
    std::string parent;
    Vec2 anchorCanvas;
    float minDeg = 0.0f;
    float maxDeg = 0.0f;

    cpFloat maxForcePivot = (cpFloat)1e8;
    cpFloat maxForceLimit = (cpFloat)1e9;

    bool enableRotSpring = false;
    cpFloat rotSpringK = (cpFloat)0.0f;
    cpFloat rotSpringDamp = (cpFloat)0.0f;
    cpFloat restAngleRadOverride = std::numeric_limits<cpFloat>::quiet_NaN();
};

struct SceneConfig {
    ScenePlacementConfig scene;
    std::unordered_map<std::string, SceneSoftOverride> softOverrides;
    SceneCollisionRulesConfig collisionRules;
    std::vector<std::string> renderOrder;
    std::vector<SceneJointConfig> joints;

    static bool loadFromFile(
        const std::string& path,
        SceneConfig& out,
        std::string& error
    );
};

}