// PhysicsScene.hpp
#pragma once

#include <chipmunk/chipmunk.h>

#include <array>
#include <cstdint>

namespace physics {

struct PhysicsSceneConfig {
    cpVect gravity = cpv(0.0, 1200.0);
    cpFloat damping = 0.95;
    int iterations = 40;
    cpFloat collisionSlop = 0.1f;
    cpFloat collisionBias = cpfpow(1.0f - 0.1f, 120.0f);
    cpTimestamp collisionPersistence = 10;

    float wallThickness = 10.0f;
    float wallMargin = 10.0f;
    bool wallsEnabled = true;

    int substeps = 8;
    cpFloat softBodyVelocityClamp = 600.0f;
};

class PhysicsScene {
public:
    PhysicsScene() = default;
    ~PhysicsScene();

    PhysicsScene(const PhysicsScene&) = delete;
    PhysicsScene& operator=(const PhysicsScene&) = delete;

    bool initialize(int width, int height, const PhysicsSceneConfig& config);
    void shutdown();

    void setWallsEnabled(bool enabled);
    bool wallsEnabled() const { return config_.wallsEnabled; }

    void resizeBounds(int width, int height);
    void step(double dtSeconds);

    cpSpace* space() { return space_; }
    const cpSpace* space() const { return space_; }

    cpBody* mouseBody() { return mouseBody_; }
    const PhysicsSceneConfig& config() const { return config_; }

private:
    void createWalls(int width, int height);
    void destroyWalls();

    cpSpace* space_ = nullptr;
    cpBody* mouseBody_ = nullptr;
    std::array<cpShape*, 4> walls_ = { nullptr, nullptr, nullptr, nullptr };

    int width_ = 0;
    int height_ = 0;
    PhysicsSceneConfig config_{};
};

} // namespace physics