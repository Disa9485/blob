// SoftBodyInteractor.hpp
#pragma once

#include "PsdAssembler.hpp"

#include <chipmunk/chipmunk.h>
#include <GLFW/glfw3.h>

#include <string>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace physics {

class PhysicsScene;

class SoftBodyInteractor {
public:
    struct Config {
        float pickRadiusPx = 28.0f;
        cpFloat springStiffness = 20000.0f;
        cpFloat springDamping = 100.0f;
        cpFloat maxForce = 2e5;
        cpFloat errorBias = cpfpow(1.0f - 0.25f, 60.0f);
    };

    SoftBodyInteractor() = default;
    ~SoftBodyInteractor();

    SoftBodyInteractor(const SoftBodyInteractor&) = delete;
    SoftBodyInteractor& operator=(const SoftBodyInteractor&) = delete;

    void setConfig(const Config& cfg) { config_ = cfg; }
    const Config& config() const { return config_; }

    void shutdown(cpSpace* space);

    void beginDrag(
        PhysicsScene& scene,
        PsdAssembly& assembly,
        float mouseX,
        float mouseY
    );

    void updateDrag(
        PhysicsScene& scene,
        float mouseX,
        float mouseY
    );

    void endDrag(cpSpace* space);

    bool isDragging() const { return grabSpring_ != nullptr; }
    bool touchedSomethingRecently() const;
    bool hasPendingTouchedParts() const;
    std::string peekTouchedPartsSentence(const std::string& userName) const;

    std::uint64_t touchEventSerial() const { return touchEventSerial_; }
    cpBody* grabbedBody() const { return grabbedBody_; }
    const RenderPart* grabbedPart() const { return grabbedPart_; }

    static std::string humanizePartId(const std::string& id);

    std::string consumeTouchedPartsSentence(const std::string& userName);

private:
    struct PickResult {
        RenderPart* part = nullptr;
        cpBody* body = nullptr;
        cpFloat dist2 = INFINITY;
        int renderPriority = -1;
    };

    PickResult pickNearestSoftBodyPoint(
        PsdAssembly& assembly,
        cpVect mouseWorld
    ) const;

    void recordTouchedPart(const std::string& partId);
    void clearTouchedParts();

    Config config_{};

    RenderPart* grabbedPart_ = nullptr;
    cpBody* grabbedBody_ = nullptr;
    cpVect grabbedLocalAnchor_ = cpvzero;
    cpConstraint* grabSpring_ = nullptr;

    std::uint64_t touchEventSerial_ = 0;
    std::vector<std::string> touchedPartsHumanReadable_;
    std::unordered_set<std::string> touchedPartsSeen_;
};

} // namespace physics