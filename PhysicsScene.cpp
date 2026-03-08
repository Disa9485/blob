// PhysicsScene.cpp
#include "PhysicsScene.hpp"

#include <algorithm>

namespace physics {
namespace {
void clampBodyVelocity(cpBody* b, cpFloat vmax) {
    const cpVect v = cpBodyGetVelocity(b);
    const cpFloat l2 = cpvlengthsq(v);
    if (l2 > vmax * vmax) {
        cpBodySetVelocity(b, cpvmult(v, vmax / cpfsqrt(l2)));
    }
}
}

PhysicsScene::~PhysicsScene() {
    shutdown();
}

bool PhysicsScene::initialize(int width, int height, const PhysicsSceneConfig& config) {
    shutdown();

    width_ = width;
    height_ = height;
    config_ = config;

    space_ = cpSpaceNew();
    if (!space_) {
        return false;
    }

    cpSpaceSetGravity(space_, config_.gravity);
    cpSpaceSetDamping(space_, config_.damping);
    cpSpaceSetIterations(space_, config_.iterations);
    cpSpaceSetCollisionSlop(space_, config_.collisionSlop);
    cpSpaceSetCollisionBias(space_, config_.collisionBias);
    cpSpaceSetCollisionPersistence(space_, config_.collisionPersistence);

    mouseBody_ = cpBodyNewKinematic();
    cpBodySetPosition(mouseBody_, cpv(0, 0));
    cpSpaceAddBody(space_, mouseBody_);

    if (config_.wallsEnabled) {
        createWalls(width_, height_);
    }

    return true;
}

void PhysicsScene::shutdown() {
    destroyWalls();

    if (mouseBody_ && space_) {
        cpSpaceRemoveBody(space_, mouseBody_);
        cpBodyFree(mouseBody_);
        mouseBody_ = nullptr;
    }

    if (space_) {
        cpSpaceFree(space_);
        space_ = nullptr;
    }

    width_ = 0;
    height_ = 0;
}

void PhysicsScene::setWallsEnabled(bool enabled) {
    config_.wallsEnabled = enabled;

    if (!space_) {
        return;
    }

    if (enabled) {
        createWalls(width_, height_);
    } else {
        destroyWalls();
    }
}

void PhysicsScene::createWalls(int width, int height) {
    if (!space_) {
        return;
    }

    destroyWalls();

    if (!config_.wallsEnabled) {
        return;
    }

    cpBody* staticBody = cpSpaceGetStaticBody(space_);

    walls_[0] = cpSegmentShapeNew(
        staticBody,
        cpv(config_.wallMargin, config_.wallMargin),
        cpv(width - config_.wallMargin, config_.wallMargin),
        config_.wallThickness
    );
    walls_[1] = cpSegmentShapeNew(
        staticBody,
        cpv(config_.wallMargin, height - config_.wallMargin),
        cpv(width - config_.wallMargin, height - config_.wallMargin),
        config_.wallThickness
    );
    walls_[2] = cpSegmentShapeNew(
        staticBody,
        cpv(config_.wallMargin, config_.wallMargin),
        cpv(config_.wallMargin, height - config_.wallMargin),
        config_.wallThickness
    );
    walls_[3] = cpSegmentShapeNew(
        staticBody,
        cpv(width - config_.wallMargin, config_.wallMargin),
        cpv(width - config_.wallMargin, height - config_.wallMargin),
        config_.wallThickness
    );

    for (cpShape* s : walls_) {
        cpShapeSetFriction(s, 1.0);
        cpShapeSetElasticity(s, 0.2);
        cpSpaceAddShape(space_, s);
    }
}

void PhysicsScene::destroyWalls() {
    if (!space_) {
        return;
    }

    for (cpShape*& s : walls_) {
        if (!s) {
            continue;
        }
        cpSpaceRemoveShape(space_, s);
        cpShapeFree(s);
        s = nullptr;
    }
}

void PhysicsScene::resizeBounds(int width, int height) {
    if (!space_) {
        return;
    }

    if (width == width_ && height == height_) {
        return;
    }

    width_ = width;
    height_ = height;
    createWalls(width_, height_);
}

void PhysicsScene::step(double dtSeconds) {
    if (!space_) {
        return;
    }

    dtSeconds = std::min(dtSeconds, 1.0 / 120.0);
    const cpFloat subDt = static_cast<cpFloat>(dtSeconds / double(config_.substeps));

    for (int i = 0; i < config_.substeps; ++i) {
        cpSpaceEachBody(space_, [](cpBody* body, void* userData) {
            const cpFloat vmax = *reinterpret_cast<cpFloat*>(userData);
            if (cpBodyGetType(body) == CP_BODY_TYPE_DYNAMIC) {
                clampBodyVelocity(body, vmax);
            }
        }, &config_.softBodyVelocityClamp);

        cpSpaceStep(space_, subDt);
    }
}

} // namespace physics