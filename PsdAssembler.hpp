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

struct JointPair {
    cpConstraint* pivot = nullptr;
    cpConstraint* framePivot = nullptr;
    cpConstraint* limit = nullptr;
    cpConstraint* rotSpring = nullptr;
};

struct PsdAssembly {
    std::string sceneId;
    std::string psdPath;
    std::string configPath;

    std::vector<RenderPart> parts;
    std::unordered_map<std::string, RenderPart*> partsByName;
    std::unordered_map<std::string, RenderItem> renderRegistry;
    std::vector<RenderItem> renderItems;

    std::vector<JointPair> joints;
    std::unique_ptr<CollisionRules> collisionRules;
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
};

} // namespace physics