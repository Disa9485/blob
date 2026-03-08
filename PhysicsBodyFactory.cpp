// PhysicsBodyFactory.cpp
#include "PhysicsBodyFactory.hpp"

#include <delaunator.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace physics {

float dot(const Vec2& a, const Vec2& b) {
    return a.x * b.x + a.y * b.y;
}

float len(const Vec2& v) {
    return std::sqrt(dot(v, v));
}

namespace {
float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

struct Edge {
    std::uint32_t a;
    std::uint32_t b;
};

struct EdgeHash {
    std::size_t operator()(const Edge& e) const noexcept {
        return (std::size_t(e.a) << 32) ^ std::size_t(e.b);
    }
};

struct EdgeEq {
    bool operator()(const Edge& x, const Edge& y) const noexcept {
        return x.a == y.a && x.b == y.b;
    }
};

Edge normalizeEdge(std::uint32_t i, std::uint32_t j) {
    if (i > j) {
        std::swap(i, j);
    }
    return { i, j };
}

static bool shouldUsePinnedVertexParams(
    const SoftPinnedBuildParams* pinnedParams,
    bool isPinnedCandidate
) {
    return pinnedParams && pinnedParams->vertex.enabled && isPinnedCandidate;
}

static cpFloat resolveMassPerPoint(
    const SoftBuildParams& params,
    const SoftPinnedBuildParams* pinnedParams,
    bool usePinned
) {
    if (usePinned && pinnedParams->vertex.overrideMassPerPoint) {
        return cpFloat(pinnedParams->vertex.massPerPoint);
    }
    return cpFloat(params.massPerPoint);
}

static cpFloat resolveRadius(
    const SoftBuildParams& params,
    const SoftPinnedBuildParams* pinnedParams,
    bool usePinned
) {
    if (usePinned && pinnedParams->vertex.overrideRadius) {
        return cpFloat(pinnedParams->vertex.radius);
    }
    return cpFloat(params.radius);
}

static cpFloat resolveFriction(
    const SoftBuildParams& params,
    const SoftPinnedBuildParams* pinnedParams,
    bool usePinned
) {
    if (usePinned && pinnedParams->vertex.overrideFriction) {
        return cpFloat(pinnedParams->vertex.friction);
    }
    return cpFloat(params.friction);
}

static cpFloat resolveElasticity(
    const SoftBuildParams& params,
    const SoftPinnedBuildParams* pinnedParams,
    bool usePinned
) {
    if (usePinned && pinnedParams->vertex.overrideElasticity) {
        return cpFloat(pinnedParams->vertex.elasticity);
    }
    return cpFloat(params.elasticity);
}

static cpFloat resolvePinnedConstraintMaxForce(
    const SoftPinnedBuildParams* pinnedParams,
    cpFloat fallbackValue
) {
    return pinnedParams ? pinnedParams->constraint.maxForce : fallbackValue;
}

static cpFloat resolvePinnedConstraintErrorBias(
    const SoftPinnedBuildParams* pinnedParams,
    cpFloat fallbackValue
) {
    return pinnedParams ? pinnedParams->constraint.errorBias : fallbackValue;
}

static cpFloat resolvePinnedConstraintMaxBias(
    const SoftPinnedBuildParams* pinnedParams,
    cpFloat fallbackValue
) {
    return pinnedParams ? pinnedParams->constraint.maxBias : fallbackValue;
}

} // namespace

std::uint8_t PhysicsBodyFactory::alphaAt(const ImageRGBA& image, int x, int y) {
    if (x < 0 || x >= image.width || y < 0 || y >= image.height) {
        return 0;
    }

    return image.rgba[(y * image.width + x) * 4 + 3];
}

OpaqueBounds PhysicsBodyFactory::computeOpaqueBounds(const ImageRGBA& image, std::uint8_t alphaThreshold) {
    OpaqueBounds bounds;
    bounds.minx = image.width;
    bounds.miny = image.height;
    bounds.maxx = -1;
    bounds.maxy = -1;

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (alphaAt(image, x, y) > alphaThreshold) {
                bounds.minx = std::min(bounds.minx, x);
                bounds.miny = std::min(bounds.miny, y);
                bounds.maxx = std::max(bounds.maxx, x);
                bounds.maxy = std::max(bounds.maxy, y);
            }
        }
    }

    bounds.valid = (bounds.maxx >= bounds.minx) && (bounds.maxy >= bounds.miny);
    return bounds;
}

Vec2 PhysicsBodyFactory::computeOpaqueCentroid(const ImageRGBA& image, std::uint8_t alphaThreshold) {
    double sx = 0.0;
    double sy = 0.0;
    double count = 0.0;

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            if (alphaAt(image, x, y) > alphaThreshold) {
                sx += x;
                sy += y;
                count += 1.0;
            }
        }
    }

    if (count <= 0.0) {
        return Vec2(image.width * 0.5f, image.height * 0.5f);
    }

    return Vec2(
        static_cast<float>(sx / count),
        static_cast<float>(sy / count)
    );
}

std::vector<Vec2> PhysicsBodyFactory::extractPerimeterPoints(
    const ImageRGBA& image,
    std::uint8_t alphaThreshold,
    float spacingPx
) {
    std::vector<Vec2> boundary;
    boundary.reserve(image.width * image.height / 16);

    for (int y = 1; y < image.height - 1; ++y) {
        for (int x = 1; x < image.width - 1; ++x) {
            if (alphaAt(image, x, y) <= alphaThreshold) {
                continue;
            }

            const bool edge =
                (alphaAt(image, x - 1, y) <= alphaThreshold) ||
                (alphaAt(image, x + 1, y) <= alphaThreshold) ||
                (alphaAt(image, x, y - 1) <= alphaThreshold) ||
                (alphaAt(image, x, y + 1) <= alphaThreshold);

            if (edge) {
                boundary.emplace_back(static_cast<float>(x), static_cast<float>(y));
            }
        }
    }

    if (boundary.size() < 16) {
        throw std::runtime_error("Not enough boundary points for soft body.");
    }

    const Vec2 center = computeOpaqueCentroid(image, alphaThreshold);

    std::sort(boundary.begin(), boundary.end(), [&](const Vec2& a, const Vec2& b) {
        const float aa = std::atan2(a.y - center.y, a.x - center.x);
        const float bb = std::atan2(b.y - center.y, b.x - center.x);
        return aa < bb;
    });

    std::vector<Vec2> perimeter;
    perimeter.reserve(boundary.size() / 4);

    Vec2 last = boundary.front();
    perimeter.push_back(last);

    float accum = 0.0f;
    for (std::size_t i = 1; i < boundary.size(); ++i) {
        const Vec2 p = boundary[i];
        accum += len(p - last);
        if (accum >= spacingPx) {
            perimeter.push_back(p);
            last = p;
            accum = 0.0f;
        }
    }

    if (perimeter.size() < 12) {
        perimeter.clear();
        constexpr std::size_t fallbackCount = 24;
        for (std::size_t i = 0; i < fallbackCount; ++i) {
            perimeter.push_back(boundary[(i * boundary.size()) / fallbackCount]);
        }
    }

    return perimeter;
}

std::vector<Vec2> PhysicsBodyFactory::extractInteriorGridPoints(
    const ImageRGBA& image,
    std::uint8_t alphaThreshold,
    float spacingPx
) {
    std::vector<Vec2> points;

    for (float y = 0.0f; y < image.height; y += spacingPx) {
        for (float x = 0.0f; x < image.width; x += spacingPx) {
            if (alphaAt(image, static_cast<int>(x), static_cast<int>(y)) > alphaThreshold) {
                points.emplace_back(x, y);
            }
        }
    }

    return points;
}

Vec2 PhysicsBodyFactory::canvasToWorld(Vec2 canvasPt, Vec2 canvasCenter, Vec2 worldPos, float scale) {
    const Vec2 delta = canvasPt - canvasCenter;
    return worldPos + delta * scale;
}

cpVect PhysicsBodyFactory::canvasToWorldCp(Vec2 canvasPt, Vec2 canvasCenter, Vec2 worldPos, float scale) {
    const Vec2 w = canvasToWorld(canvasPt, canvasCenter, worldPos, scale);
    return cpv(static_cast<cpFloat>(w.x), static_cast<cpFloat>(w.y));
}

void PhysicsBodyFactory::addConstraint(cpSpace* space, SoftBody& body, cpConstraint* c) {
    cpSpaceAddConstraint(space, c);
    body.constraints.push_back(c);
}

RigidBodyData PhysicsBodyFactory::createRigidBodyFromImage(
    cpSpace* space,
    const std::string& name,
    const ImageRGBA& image,
    Vec2 canvasCenter,
    Vec2 worldPos,
    float scale,
    const RigidBuildParams& params
) {
    constexpr std::uint8_t alphaThreshold = 10;

    RigidBodyData out;
    out.name = name;

    OpaqueBounds ob = computeOpaqueBounds(image, alphaThreshold);
    if (!ob.valid) {
        ob.minx = 0;
        ob.miny = 0;
        ob.maxx = 1;
        ob.maxy = 1;
        ob.valid = true;
    }

    constexpr int pad = 2;
    ob.minx = std::max(0, ob.minx - pad);
    ob.miny = std::max(0, ob.miny - pad);
    ob.maxx = std::min(image.width - 1, ob.maxx + pad);
    ob.maxy = std::min(image.height - 1, ob.maxy + pad);

    const float boxWCanvas = float(ob.maxx - ob.minx + 1);
    const float boxHCanvas = float(ob.maxy - ob.miny + 1);

    out.anchorCanvas = Vec2(
        float(ob.minx) + boxWCanvas * 0.5f,
        float(ob.miny) + boxHCanvas * 0.5f
    );

    const Vec2 worldCenter = canvasToWorld(out.anchorCanvas, canvasCenter, worldPos, scale);

    const float boxWWorld = boxWCanvas * scale;
    const float boxHWorld = boxHCanvas * scale;

    const cpFloat mass = params.density * cpFloat(boxWWorld * boxHWorld);
    const cpFloat moment = cpMomentForBox(mass, boxWWorld, boxHWorld);

    out.body = cpBodyNew(mass, moment);
    cpBodySetPosition(out.body, cpv(worldCenter.x, worldCenter.y));

    out.shape = cpBoxShapeNew(out.body, boxWWorld, boxHWorld, 0.0);
    cpShapeSetFriction(out.shape, params.friction);
    cpShapeSetElasticity(out.shape, params.elasticity);

    if (params.collisionGroup != 0) {
        cpShapeSetFilter(
            out.shape,
            cpShapeFilterNew(params.collisionGroup, CP_ALL_CATEGORIES, CP_ALL_CATEGORIES)
        );
    }

    cpSpaceAddBody(space, out.body);
    cpSpaceAddShape(space, out.shape);

    out.halfWidthWorld = boxWWorld * 0.5f;
    out.halfHeightWorld = boxHWorld * 0.5f;
    out.opaqueBounds = ob;

    return out;
}

SoftBodyData PhysicsBodyFactory::createSoftBodyFromAlpha(
    cpSpace* space,
    const std::string& name,
    const ImageRGBA& image,
    Vec2 canvasCenter,
    Vec2 worldPos,
    const SoftBuildParams& params,
    const SoftPinnedBuildParams* pinnedParams
) {
    constexpr std::uint8_t alphaThreshold = 2;

    SoftBodyData out;
    out.name = name;

    auto perimeter = extractPerimeterPoints(image, alphaThreshold, params.pointSpacing * 0.5f);
    auto interior = extractInteriorGridPoints(image, alphaThreshold, params.pointSpacing);

    if (interior.size() < 10) {
        interior = extractInteriorGridPoints(image, alphaThreshold, std::max(6.0f, params.pointSpacing * 0.5f));
    }

    const std::size_t maxPerimeter = 200;
    if (perimeter.size() > maxPerimeter) {
        std::vector<Vec2> capped;
        capped.reserve(maxPerimeter);
        for (std::size_t i = 0; i < maxPerimeter; ++i) {
            capped.push_back(perimeter[(i * perimeter.size()) / maxPerimeter]);
        }
        perimeter.swap(capped);
    }

    out.body.perimeterCount = static_cast<int>(perimeter.size());
    out.body.modelVerts.reserve(perimeter.size() + interior.size());
    out.body.modelVerts.insert(out.body.modelVerts.end(), perimeter.begin(), perimeter.end());
    out.body.modelVerts.insert(out.body.modelVerts.end(), interior.begin(), interior.end());

    out.body.uvs.resize(out.body.modelVerts.size());
    for (std::size_t i = 0; i < out.body.modelVerts.size(); ++i) {
        out.body.uvs[i].x = out.body.modelVerts[i].x / float(image.width);
        out.body.uvs[i].y = out.body.modelVerts[i].y / float(image.height);
    }

    const Vec2 modelCenter = computeOpaqueCentroid(image, alphaThreshold);
    out.anchorCanvas = modelCenter;

    std::vector<Vec2> worldVerts(out.body.modelVerts.size());
    for (std::size_t i = 0; i < out.body.modelVerts.size(); ++i) {
        Vec2 v = out.body.modelVerts[i] - modelCenter;
        v = v * params.scale;
        v = v + worldPos;
        worldVerts[i] = v;
    }

    // Precompute which vertices are inside the attach radius.
    // For alpha bodies, keep the existing behavior: only interior vertices can be pinned.
    std::vector<std::uint8_t> attachCandidate(worldVerts.size(), 0);
    const cpFloat r2 = cpFloat(params.attachRadiusWorld * params.attachRadiusWorld);

    for (std::size_t i = out.body.perimeterCount; i < worldVerts.size(); ++i) {
        const cpVect wp = cpv(worldVerts[i].x, worldVerts[i].y);
        if (cpvlengthsq(cpvsub(wp, params.attachWorld)) <= r2) {
            attachCandidate[i] = 1;
        }
    }

    out.body.bodies.reserve(worldVerts.size());
    out.body.shapes.reserve(worldVerts.size());

    for (std::size_t i = 0; i < worldVerts.size(); ++i) {
        const bool usePinned = shouldUsePinnedVertexParams(pinnedParams, attachCandidate[i] != 0);

        const cpFloat massPerPoint = resolveMassPerPoint(params, pinnedParams, usePinned);
        const cpFloat radius = resolveRadius(params, pinnedParams, usePinned);
        const cpFloat friction = resolveFriction(params, pinnedParams, usePinned);
        const cpFloat elasticity = resolveElasticity(params, pinnedParams, usePinned);

        const Vec2& p = worldVerts[i];
        const cpFloat moment = cpMomentForCircle(massPerPoint, 0.0, radius, cpvzero);
        cpBody* body = cpBodyNew(massPerPoint, moment);
        cpBodySetPosition(body, cpv(p.x, p.y));

        cpShape* shape = cpCircleShapeNew(body, radius, cpvzero);
        cpShapeSetFriction(shape, friction);
        cpShapeSetElasticity(shape, elasticity);

        if (params.collisionGroup != 0) {
            cpShapeSetFilter(
                shape,
                cpShapeFilterNew(params.collisionGroup, CP_ALL_CATEGORIES, CP_ALL_CATEGORIES)
            );
        }

        cpSpaceAddBody(space, body);
        cpSpaceAddShape(space, shape);

        out.body.bodies.push_back(body);
        out.body.shapes.push_back(shape);
    }

    std::vector<double> coords;
    coords.reserve(out.body.modelVerts.size() * 2);
    for (const auto& v : out.body.modelVerts) {
        coords.push_back(double(v.x));
        coords.push_back(double(v.y));
    }

    delaunator::Delaunator d(coords);

    out.body.indices.clear();
    out.body.indices.reserve(d.triangles.size());
    for (auto t : d.triangles) {
        out.body.indices.push_back(static_cast<std::uint32_t>(t));
    }

    auto alphaAtFloat = [&](float fx, float fy) -> std::uint8_t {
        const int x = static_cast<int>(std::floor(fx + 0.5f));
        const int y = static_cast<int>(std::floor(fy + 0.5f));
        return alphaAt(image, x, y);
    };

    auto triHasOpaque = [&](const Vec2& a, const Vec2& b, const Vec2& c) -> bool {
        const Vec2 mAB{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f };
        const Vec2 mBC{ (b.x + c.x) * 0.5f, (b.y + c.y) * 0.5f };
        const Vec2 mCA{ (c.x + a.x) * 0.5f, (c.y + a.y) * 0.5f };
        const Vec2 center{ (a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f };

        return
            alphaAtFloat(a.x, a.y) > alphaThreshold ||
            alphaAtFloat(b.x, b.y) > alphaThreshold ||
            alphaAtFloat(c.x, c.y) > alphaThreshold ||
            alphaAtFloat(mAB.x, mAB.y) > alphaThreshold ||
            alphaAtFloat(mBC.x, mBC.y) > alphaThreshold ||
            alphaAtFloat(mCA.x, mCA.y) > alphaThreshold ||
            alphaAtFloat(center.x, center.y) > alphaThreshold;
    };

    std::vector<std::uint32_t> filtered;
    filtered.reserve(out.body.indices.size());

    for (std::size_t i = 0; i + 2 < out.body.indices.size(); i += 3) {
        const std::uint32_t ia = out.body.indices[i + 0];
        const std::uint32_t ib = out.body.indices[i + 1];
        const std::uint32_t ic = out.body.indices[i + 2];

        const Vec2 a = out.body.modelVerts[ia];
        const Vec2 b = out.body.modelVerts[ib];
        const Vec2 c = out.body.modelVerts[ic];

        if (triHasOpaque(a, b, c)) {
            filtered.push_back(ia);
            filtered.push_back(ib);
            filtered.push_back(ic);
        }
    }

    out.body.indices.swap(filtered);

    std::unordered_set<Edge, EdgeHash, EdgeEq> edges;
    edges.reserve(out.body.indices.size());

    for (std::size_t i = 0; i + 2 < out.body.indices.size(); i += 3) {
        const auto a = out.body.indices[i + 0];
        const auto b = out.body.indices[i + 1];
        const auto c = out.body.indices[i + 2];

        edges.insert(normalizeEdge(a, b));
        edges.insert(normalizeEdge(b, c));
        edges.insert(normalizeEdge(c, a));
    }

    constexpr bool useEdgeLimits = true;

    for (const auto& e : edges) {
        cpBody* bi = out.body.bodies[e.a];
        cpBody* bj = out.body.bodies[e.b];

        const cpVect pi = cpBodyGetPosition(bi);
        const cpVect pj = cpBodyGetPosition(bj);
        const cpFloat restLen = cpvlength(cpvsub(pi, pj));

        cpConstraint* spring = cpDampedSpringNew(
            bi, bj, cpvzero, cpvzero, restLen, params.springK, params.springDamping
        );
        addConstraint(space, out.body, spring);

        if (useEdgeLimits) {
            cpConstraint* limit = cpSlideJointNew(
                bi, bj, cpvzero, cpvzero,
                restLen * params.stretchMinRatio,
                restLen * params.stretchMaxRatio
            );
            addConstraint(space, out.body, limit);
        }
    }

    for (int i = 0; i < out.body.perimeterCount; ++i) {
        const int j = (i + 1) % out.body.perimeterCount;

        cpBody* bi = out.body.bodies[i];
        cpBody* bj = out.body.bodies[j];

        const cpVect pi = cpBodyGetPosition(bi);
        const cpVect pj = cpBodyGetPosition(bj);
        const cpFloat restLen = cpvlength(cpvsub(pi, pj));

        cpConstraint* spring = cpDampedSpringNew(
            bi, bj, cpvzero, cpvzero, restLen, params.springK * 1.5f, params.springDamping
        );
        addConstraint(space, out.body, spring);

        if (useEdgeLimits) {
            cpConstraint* limit = cpSlideJointNew(
                bi, bj, cpvzero, cpvzero,
                restLen * 0.9f,
                restLen * 1.15f
            );
            addConstraint(space, out.body, limit);
        }
    }

    const cpFloat bendK = params.springK * params.perimeterBendKRatio;
    for (int i = 0; i < out.body.perimeterCount; ++i) {
        const int j = (i + 2) % out.body.perimeterCount;

        cpBody* bi = out.body.bodies[i];
        cpBody* bj = out.body.bodies[j];

        const cpVect pi = cpBodyGetPosition(bi);
        const cpVect pj = cpBodyGetPosition(bj);
        const cpFloat restLen = cpvlength(cpvsub(pi, pj));

        cpConstraint* spring = cpDampedSpringNew(
            bi, bj, cpvzero, cpvzero, restLen, bendK, params.springDamping
        );
        addConstraint(space, out.body, spring);
    }

    const cpFloat centerMass = cpFloat(std::max(1.0, double(out.body.bodies.size()) * double(params.massPerPoint) * 0.25));
    const cpFloat centerMoment = cpMomentForCircle(centerMass, 0.0, cpFloat(params.radius * 6.0f), cpvzero);

    out.body.centerBody = cpBodyNew(centerMass, centerMoment);
    cpBodySetPosition(out.body.centerBody, cpv(worldPos.x, worldPos.y));
    cpBodySetAngle(out.body.centerBody, 0.0);
    cpSpaceAddBody(space, out.body.centerBody);

    out.body.pinned.assign(out.body.bodies.size(), 0);

    for (int i = out.body.perimeterCount; i < static_cast<int>(out.body.bodies.size()); ++i) {
        if (!attachCandidate[i]) {
            continue;
        }

        cpBody* bi = out.body.bodies[i];
        const cpVect pi = cpBodyGetPosition(bi);

        out.body.pinned[i] = 1;

        const cpVect aLocal = cpBodyWorldToLocal(out.body.centerBody, pi);
        const cpVect bLocal = cpBodyWorldToLocal(bi, pi);

        cpConstraint* pin = cpPivotJointNew2(out.body.centerBody, bi, aLocal, bLocal);
        cpConstraintSetMaxForce(pin, resolvePinnedConstraintMaxForce(pinnedParams, cpFloat(1e7)));
        cpConstraintSetErrorBias(pin, resolvePinnedConstraintErrorBias(pinnedParams, cpfpow(1.0f - 0.05f, 60.0f)));
        cpConstraintSetMaxBias(pin, resolvePinnedConstraintMaxBias(pinnedParams, cpFloat(1e7)));
        addConstraint(space, out.body, pin);
    }

    const cpFloat centerK = params.springK * params.centerSpringKRatio;

    for (int i = 0; i < out.body.perimeterCount; ++i) {
        cpBody* bi = out.body.bodies[i];

        const cpVect pi = cpBodyGetPosition(bi);
        const cpVect pc = cpBodyGetPosition(out.body.centerBody);
        const cpFloat restLen = cpvlength(cpvsub(pi, pc));

        cpConstraint* spring = cpDampedSpringNew(
            out.body.centerBody, bi, cpvzero, cpvzero, restLen, centerK, params.springDamping
        );
        addConstraint(space, out.body, spring);

        if (useEdgeLimits) {
            cpConstraint* limit = cpSlideJointNew(
                out.body.centerBody, bi, cpvzero, cpvzero,
                restLen * 0.75f,
                restLen * 1.25f
            );
            addConstraint(space, out.body, limit);
        }
    }

    return out;
}

SoftBodyData PhysicsBodyFactory::createSoftBodyFromMesh(
    cpSpace* space,
    const std::string& name,
    const MeshData& mesh,
    Vec2 canvasCenter,
    Vec2 worldPos,
    const SoftBuildParams& params,
    const SoftPinnedBuildParams* pinnedParams
) {
    SoftBodyData out;
    out.name = name;

    out.body.modelVerts = mesh.verticesPx;
    out.body.uvs = mesh.uvs;
    out.body.indices = mesh.indices;
    out.body.perimeterCount = 0;

    cpVect avg = cpvzero;
    std::vector<cpVect> worldPositions;
    worldPositions.reserve(mesh.verticesPx.size());

    for (const auto& v : mesh.verticesPx) {
        const cpVect wp = canvasToWorldCp(v, canvasCenter, worldPos, params.scale);
        worldPositions.push_back(wp);
        avg = cpvadd(avg, wp);
    }

    avg = cpvmult(avg, cpFloat(1.0 / double(worldPositions.size())));

    // Boundary detection first, so we can preserve the current behavior:
    // only non-boundary vertices can become pinned.
    std::unordered_map<Edge, int, EdgeHash, EdgeEq> edgeUse;
    edgeUse.reserve(out.body.indices.size());

    for (std::size_t i = 0; i + 2 < out.body.indices.size(); i += 3) {
        const auto a = out.body.indices[i + 0];
        const auto b = out.body.indices[i + 1];
        const auto c = out.body.indices[i + 2];

        edgeUse[normalizeEdge(a, b)]++;
        edgeUse[normalizeEdge(b, c)]++;
        edgeUse[normalizeEdge(c, a)]++;
    }

    std::vector<std::uint8_t> isBoundary(mesh.verticesPx.size(), 0);
    for (const auto& kv : edgeUse) {
        if (kv.second == 1) {
            isBoundary[kv.first.a] = 1;
            isBoundary[kv.first.b] = 1;
        }
    }

    const cpFloat r2 = cpFloat(params.attachRadiusWorld * params.attachRadiusWorld);
    std::vector<std::uint8_t> attachCandidate(worldPositions.size(), 0);

    for (std::size_t i = 0; i < worldPositions.size(); ++i) {
        if (isBoundary[i]) {
            continue;
        }

        if (cpvlengthsq(cpvsub(worldPositions[i], params.attachWorld)) <= r2) {
            attachCandidate[i] = 1;
        }
    }

    for (std::size_t i = 0; i < worldPositions.size(); ++i) {
        const bool usePinned = shouldUsePinnedVertexParams(pinnedParams, attachCandidate[i] != 0);

        const cpFloat massPerPoint = resolveMassPerPoint(params, pinnedParams, usePinned);
        const cpFloat radius = resolveRadius(params, pinnedParams, usePinned);
        const cpFloat friction = resolveFriction(params, pinnedParams, usePinned);
        const cpFloat elasticity = resolveElasticity(params, pinnedParams, usePinned);

        const cpVect wp = worldPositions[i];
        const cpFloat moment = cpMomentForCircle(massPerPoint, 0.0, radius, cpvzero);
        cpBody* body = cpBodyNew(massPerPoint, moment);
        cpBodySetPosition(body, wp);

        cpShape* shape = cpCircleShapeNew(body, radius, cpvzero);
        cpShapeSetFriction(shape, friction);
        cpShapeSetElasticity(shape, elasticity);

        if (params.collisionGroup != 0) {
            cpShapeSetFilter(
                shape,
                cpShapeFilterNew(params.collisionGroup, CP_ALL_CATEGORIES, CP_ALL_CATEGORIES)
            );
        }

        cpSpaceAddBody(space, body);
        cpSpaceAddShape(space, shape);

        out.body.bodies.push_back(body);
        out.body.shapes.push_back(shape);
    }

    std::unordered_set<Edge, EdgeHash, EdgeEq> edges;
    edges.reserve(out.body.indices.size());

    for (std::size_t i = 0; i + 2 < out.body.indices.size(); i += 3) {
        const auto a = out.body.indices[i + 0];
        const auto b = out.body.indices[i + 1];
        const auto c = out.body.indices[i + 2];

        edges.insert(normalizeEdge(a, b));
        edges.insert(normalizeEdge(b, c));
        edges.insert(normalizeEdge(c, a));
    }

    constexpr bool useEdgeLimits = true;

    for (const auto& e : edges) {
        cpBody* bi = out.body.bodies[e.a];
        cpBody* bj = out.body.bodies[e.b];

        const cpVect pi = cpBodyGetPosition(bi);
        const cpVect pj = cpBodyGetPosition(bj);
        const cpFloat restLen = cpvlength(cpvsub(pi, pj));

        cpConstraint* spring = cpDampedSpringNew(
            bi, bj, cpvzero, cpvzero, restLen, params.springK, params.springDamping
        );
        addConstraint(space, out.body, spring);

        if (useEdgeLimits) {
            cpConstraint* limit = cpSlideJointNew(
                bi, bj, cpvzero, cpvzero,
                restLen * params.stretchMinRatio,
                restLen * params.stretchMaxRatio
            );
            addConstraint(space, out.body, limit);
        }
    }

    const cpFloat centerMass = cpFloat(std::max(1.0, double(out.body.bodies.size()) * double(params.massPerPoint) * 0.25));
    const cpFloat centerMoment = cpMomentForCircle(centerMass, 0.0, cpFloat(params.radius * 6.0f), cpvzero);

    out.body.centerBody = cpBodyNew(centerMass, centerMoment);
    cpBodySetPosition(out.body.centerBody, avg);
    cpBodySetAngle(out.body.centerBody, 0.0);
    cpSpaceAddBody(space, out.body.centerBody);

    const cpFloat centerK = params.springK * params.centerSpringKRatio;
    for (cpBody* bi : out.body.bodies) {
        const cpVect pi = cpBodyGetPosition(bi);
        const cpVect pc = cpBodyGetPosition(out.body.centerBody);
        const cpFloat restLen = cpvlength(cpvsub(pi, pc));

        cpConstraint* spring = cpDampedSpringNew(
            out.body.centerBody, bi, cpvzero, cpvzero, restLen, centerK, params.springDamping
        );
        addConstraint(space, out.body, spring);
    }

    out.body.pinned.assign(out.body.bodies.size(), 0);

    for (int i = 0; i < static_cast<int>(out.body.bodies.size()); ++i) {
        if (!attachCandidate[i]) {
            continue;
        }

        cpBody* bi = out.body.bodies[i];
        const cpVect pi = cpBodyGetPosition(bi);

        out.body.pinned[i] = 1;

        const cpVect aLocal = cpBodyWorldToLocal(out.body.centerBody, pi);
        const cpVect bLocal = cpBodyWorldToLocal(bi, pi);

        cpConstraint* pin = cpPivotJointNew2(out.body.centerBody, bi, aLocal, bLocal);
        cpConstraintSetMaxForce(pin, resolvePinnedConstraintMaxForce(pinnedParams, cpFloat(1e5)));
        cpConstraintSetErrorBias(pin, resolvePinnedConstraintErrorBias(pinnedParams, cpfpow(1.0f - 0.05f, 60.0f)));
        cpConstraintSetMaxBias(pin, resolvePinnedConstraintMaxBias(pinnedParams, cpFloat(1e5)));
        addConstraint(space, out.body, pin);
    }

    if (!mesh.verticesPx.empty()) {
        out.anchorCanvas = mesh.verticesPx.front();
    }

    return out;
}

void PhysicsBodyFactory::destroyRigidBody(cpSpace* space, RigidBodyData& data) {
    if (data.shape) {
        cpSpaceRemoveShape(space, data.shape);
        cpShapeFree(data.shape);
        data.shape = nullptr;
    }

    if (data.body) {
        cpSpaceRemoveBody(space, data.body);
        cpBodyFree(data.body);
        data.body = nullptr;
    }
}

void PhysicsBodyFactory::destroySoftBody(cpSpace* space, SoftBodyData& data) {
    for (cpConstraint* c : data.body.constraints) {
        cpSpaceRemoveConstraint(space, c);
        cpConstraintFree(c);
    }
    data.body.constraints.clear();

    for (cpShape* s : data.body.shapes) {
        cpSpaceRemoveShape(space, s);
        cpShapeFree(s);
    }
    data.body.shapes.clear();

    for (cpBody* b : data.body.bodies) {
        cpSpaceRemoveBody(space, b);
        cpBodyFree(b);
    }
    data.body.bodies.clear();

    if (data.body.centerBody) {
        cpSpaceRemoveBody(space, data.body.centerBody);
        cpBodyFree(data.body.centerBody);
        data.body.centerBody = nullptr;
    }
}

} // namespace physics