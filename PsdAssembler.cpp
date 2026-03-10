// PsdAssembler.cpp
#include "PsdAssembler.hpp"

#include "GameRenderer.hpp"
#include "PhysicsBodyFactory.hpp"
#include "PsdLoader.hpp"
#include "RenderRegistry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <unordered_set>

namespace physics {
namespace {

static inline std::uint64_t pairKey(std::uint32_t a, std::uint32_t b) {
    if (a > b) std::swap(a, b);
    return (std::uint64_t(a) << 32) | std::uint64_t(b);
}

static bool parseOverlayName(const std::string& layerName, std::string& outBaseName) {
    const std::string key = "_overlay";
    auto pos = layerName.find(key);
    if (pos == std::string::npos) return false;
    outBaseName = layerName.substr(0, pos);
    return !outBaseName.empty();
}

static Vec2 canvasToWorld(Vec2 canvasPt, Vec2 canvasCenter, Vec2 worldPos, float scale) {
    return worldPos + (canvasPt - canvasCenter) * scale;
}

static cpVect canvasToWorldCp(Vec2 canvasPt, Vec2 canvasCenter, Vec2 worldPos, float scale) {
    Vec2 w = canvasToWorld(canvasPt, canvasCenter, worldPos, scale);
    return cpv((cpFloat)w.x, (cpFloat)w.y);
}

static bool initializeDialogueAnchorRuntime(
    const SceneDialogueAnchorConfig& cfg,
    const std::unordered_map<std::string, RenderPart*>& partsByName,
    Vec2 canvasCenter,
    Vec2 sceneWorldPos,
    float sceneScale,
    DialogueAnchorRuntime& out
) {
    out = DialogueAnchorRuntime{};

    if (!cfg.enabled) {
        return true;
    }

    auto it = partsByName.find(cfg.part);
    if (it == partsByName.end() || !it->second) {
        std::cerr << "[dialogue_anchor] unknown part: " << cfg.part << "\n";
        return false;
    }

    RenderPart* part = it->second;
    const cpVect anchorWorld = canvasToWorldCp(cfg.attach, canvasCenter, sceneWorldPos, sceneScale);

    out.valid = true;
    out.partName = cfg.part;
    out.part = part;
    out.attachCanvas = cfg.attach;

    if (part->kind == PartKind::Rigid) {
        if (!part->rigid.body) {
            std::cerr << "[dialogue_anchor] rigid part has no body: " << cfg.part << "\n";
            out.valid = false;
            return false;
        }

        out.rigidLocal = cpBodyWorldToLocal(part->rigid.body, anchorWorld);
        return true;
    }

    cpBody* bestBody = nullptr;
    int bestIndex = -1;
    cpFloat bestDist2 = (cpFloat)1e18;

    for (int i = 0; i < static_cast<int>(part->soft.body.bodies.size()); ++i) {
        cpBody* b = part->soft.body.bodies[i];
        if (!b) {
            continue;
        }

        const cpVect p = cpBodyGetPosition(b);
        const cpFloat d2 = cpvlengthsq(cpvsub(p, anchorWorld));
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestBody = b;
            bestIndex = i;
        }
    }

    if (!bestBody || bestIndex < 0) {
        std::cerr << "[dialogue_anchor] soft part has no vertex bodies: " << cfg.part << "\n";
        out.valid = false;
        return false;
    }

    out.softVertexIndex = bestIndex;
    out.softVertexLocalOffset = cpBodyWorldToLocal(bestBody, anchorWorld);
    return true;
}

static bool getDialogueAnchorWorldPosition(
    const DialogueAnchorRuntime& anchor,
    cpVect& outWorld
) {
    if (!anchor.valid || !anchor.part) {
        return false;
    }

    if (anchor.part->kind == PartKind::Rigid) {
        if (!anchor.part->rigid.body) {
            return false;
        }

        outWorld = cpBodyLocalToWorld(anchor.part->rigid.body, anchor.rigidLocal);
        return true;
    }

    if (anchor.softVertexIndex < 0 ||
        anchor.softVertexIndex >= static_cast<int>(anchor.part->soft.body.bodies.size())) {
        return false;
    }

    cpBody* b = anchor.part->soft.body.bodies[anchor.softVertexIndex];
    if (!b) {
        return false;
    }

    outWorld = cpBodyLocalToWorld(b, anchor.softVertexLocalOffset);
    return true;
}

static ImageRGBA makeImageFromLayer(const LayerImageRGBA& layer) {
    ImageRGBA img;
    img.width = layer.width;
    img.height = layer.height;
    img.rgba = layer.rgba;
    return img;
}

static bool loadMeshJson(const std::string& path, MeshData& out, std::string& error) {
    using nlohmann::json;

    std::ifstream f(path);
    if (!f.is_open()) {
        error = "Failed to open mesh json: " + path;
        return false;
    }

    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        error = std::string("Failed to parse mesh json: ") + e.what();
        return false;
    }

    out.imageWidth = j.value("image_width", 0);
    out.imageHeight = j.value("image_height", 0);

    out.verticesPx.clear();
    for (auto& v : j.at("vertices_px")) {
        out.verticesPx.emplace_back((float)v[0].get<double>(), (float)v[1].get<double>());
    }

    out.uvs.clear();
    for (auto& uv : j.at("uvs")) {
        out.uvs.emplace_back((float)uv[0].get<double>(), (float)uv[1].get<double>());
    }

    out.indices.clear();
    for (auto& tri : j.at("triangles")) {
        out.indices.push_back((std::uint32_t)tri[0].get<int>());
        out.indices.push_back((std::uint32_t)tri[1].get<int>());
        out.indices.push_back((std::uint32_t)tri[2].get<int>());
    }

    if (out.verticesPx.size() < 3 || out.indices.size() < 3) {
        error = "Mesh json too small: " + path;
        return false;
    }
    if (out.uvs.size() != out.verticesPx.size()) {
        error = "Mesh json uv count mismatch: " + path;
        return false;
    }

    return true;
}

static Vec2 computeOpaqueCentroid(const ImageRGBA& image, std::uint8_t alphaThreshold) {
    double sx = 0.0;
    double sy = 0.0;
    double count = 0.0;

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::uint8_t a = image.rgba[(y * image.width + x) * 4 + 3];
            if (a > alphaThreshold) {
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

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static Vec2 resolveSceneWorldPosition(
    const ScenePlacementConfig& placement,
    int sceneWidth,
    int sceneHeight
) {
    return Vec2(
        clamp01(placement.spawnPosition.x) * (float)sceneWidth,
        clamp01(placement.spawnPosition.y) * (float)sceneHeight
    );
}

static bool isSceneAnchorName(const std::string& name) {
    return name.empty() || name == "scene";
}

static cpBody* pickAngleBodyForJoint(RenderPart* p) {
    if (!p) return nullptr;
    if (p->kind == PartKind::Rigid) return p->rigid.body;
    return p->soft.body.centerBody;
}

static cpBody* pickBodyForJoint(RenderPart* p, cpVect worldAnchor) {
    if (!p) return nullptr;

    if (p->kind == PartKind::Rigid) {
        return p->rigid.body;
    }

    cpBody* best = nullptr;
    cpFloat bestD = (cpFloat)1e18;
    for (cpBody* b : p->soft.body.bodies) {
        cpFloat d = cpvlengthsq(cpvsub(cpBodyGetPosition(b), worldAnchor));
        if (d < bestD) {
            bestD = d;
            best = b;
        }
    }
    return best;
}

static inline cpFloat degToRad(float deg) {
    return (cpFloat)(deg * 3.14159265358979323846 / 180.0);
}

static JointPair connectPartsPivotLimited(
    cpSpace* space,
    std::unordered_map<std::string, RenderPart*>& partsByName,
    const SceneJointConfig& cfg,
    Vec2 canvasCenter,
    Vec2 worldPos,
    float scale
) {
    JointPair out{};

    auto itA = partsByName.find(cfg.child);
    if (itA == partsByName.end()) {
        std::cerr << "[joint] missing child part: " << cfg.child << "\n";
        return out;
    }

    RenderPart* a = itA->second;
    RenderPart* b = nullptr;

    const bool parentIsScene = isSceneAnchorName(cfg.parent);
    if (!parentIsScene) {
        auto itB = partsByName.find(cfg.parent);
        if (itB == partsByName.end()) {
            std::cerr << "[joint] missing parent part: " << cfg.parent << "\n";
            return out;
        }
        b = itB->second;
    }

    const cpVect pWorld = canvasToWorldCp(cfg.anchorCanvas, canvasCenter, worldPos, scale);

    cpBody* aBody = pickBodyForJoint(a, pWorld);
    if (!aBody) {
        std::cerr << "[joint] failed to resolve child body for: " << cfg.child << "\n";
        return out;
    }

    if (parentIsScene) {
        cpBody* staticBody = cpSpaceGetStaticBody(space);

        // Lock child translation to a fixed world point.
        out.pivot = cpPivotJointNew(
            staticBody,
            aBody,
            pWorld
        );
        cpConstraintSetMaxForce(out.pivot, cfg.maxForcePivot);
        cpSpaceAddConstraint(space, out.pivot);

        // Also constrain rotation against the scene.
        cpBody* aAngleBody = pickAngleBodyForJoint(a);
        if (aAngleBody) {
            cpFloat minRad = degToRad(cfg.minDeg);
            cpFloat maxRad = degToRad(cfg.maxDeg);
            if (minRad > maxRad) std::swap(minRad, maxRad);

            // Interpret limits relative to the current world angle.
            const cpFloat baseAngle = cpBodyGetAngle(aAngleBody);

            out.limit = cpRotaryLimitJointNew(
                staticBody,
                aAngleBody,
                baseAngle + minRad,
                baseAngle + maxRad
            );
            cpConstraintSetMaxForce(out.limit, cfg.maxForceLimit);
            cpSpaceAddConstraint(space, out.limit);

            if (cfg.enableRotSpring && cfg.rotSpringK > 0.0f) {
                const cpFloat restAngle =
                    std::isnan((double)cfg.restAngleRadOverride)
                        ? baseAngle
                        : (baseAngle + cfg.restAngleRadOverride);

                out.rotSpring = cpDampedRotarySpringNew(
                    staticBody,
                    aAngleBody,
                    restAngle,
                    cfg.rotSpringK,
                    cfg.rotSpringDamp
                );
                cpConstraintSetMaxForce(out.rotSpring, (cpFloat)1e10);
                cpConstraintSetMaxBias(out.rotSpring, (cpFloat)1e6);
                cpSpaceAddConstraint(space, out.rotSpring);
            }
        }

        return out;
    }

    cpBody* bBody = pickBodyForJoint(b, pWorld);
    if (!bBody) {
        std::cerr << "[joint] failed to resolve parent body for: " << cfg.parent << "\n";
        return out;
    }

    const cpVect aLocal = cpBodyWorldToLocal(aBody, pWorld);
    const cpVect bLocal = cpBodyWorldToLocal(bBody, pWorld);

    out.pivot = cpPivotJointNew2(aBody, bBody, aLocal, bLocal);
    cpConstraintSetMaxForce(out.pivot, cfg.maxForcePivot);
    cpSpaceAddConstraint(space, out.pivot);

    cpFloat minRad = degToRad(cfg.minDeg);
    cpFloat maxRad = degToRad(cfg.maxDeg);
    if (minRad > maxRad) std::swap(minRad, maxRad);

    cpBody* aAngleBody = pickAngleBodyForJoint(a);
    cpBody* bAngleBody = pickAngleBodyForJoint(b);

    if (aAngleBody && bAngleBody) {
        const cpVect aFrameLocal = cpBodyWorldToLocal(aAngleBody, pWorld);
        const cpVect bFrameLocal = cpBodyWorldToLocal(bAngleBody, pWorld);

        out.framePivot = cpPivotJointNew2(aAngleBody, bAngleBody, aFrameLocal, bFrameLocal);
        cpConstraintSetMaxForce(out.framePivot, (cpFloat)1e10);
        cpSpaceAddConstraint(space, out.framePivot);

        out.limit = cpRotaryLimitJointNew(aAngleBody, bAngleBody, minRad, maxRad);
        cpConstraintSetMaxForce(out.limit, cfg.maxForceLimit);
        cpSpaceAddConstraint(space, out.limit);

        if (cfg.enableRotSpring && cfg.rotSpringK > 0.0f) {
            const cpFloat restAngle =
                std::isnan((double)cfg.restAngleRadOverride)
                    ? cpBodyGetAngle(aAngleBody) - cpBodyGetAngle(bAngleBody)
                    : cfg.restAngleRadOverride;

            out.rotSpring = cpDampedRotarySpringNew(
                aAngleBody,
                bAngleBody,
                restAngle,
                cfg.rotSpringK,
                cfg.rotSpringDamp
            );
            cpConstraintSetMaxForce(out.rotSpring, (cpFloat)1e10);
            cpConstraintSetMaxBias(out.rotSpring, (cpFloat)1e6);
            cpSpaceAddConstraint(space, out.rotSpring);
        }
    }

    return out;
}

static cpBool allowListBegin(cpArbiter* arb, cpSpace* space, cpDataPointer) {
    cpShape* aShape = nullptr;
    cpShape* bShape = nullptr;
    cpArbiterGetShapes(arb, &aShape, &bShape);

    auto* tagA = reinterpret_cast<ShapeTag*>(cpShapeGetUserData(aShape));
    auto* tagB = reinterpret_cast<ShapeTag*>(cpShapeGetUserData(bShape));

    if (!tagA || !tagB || tagA->partId == 0 || tagB->partId == 0) return cpTrue;
    if (tagA->partId == tagB->partId) return cpFalse;

    auto* rules = reinterpret_cast<CollisionRules*>(cpSpaceGetUserData(space));
    if (!rules) return cpFalse;

    const std::uint64_t key = pairKey(tagA->partId, tagB->partId);
    return (rules->allowedPairs.find(key) != rules->allowedPairs.end()) ? cpTrue : cpFalse;
}

static bool isEyeLayerName(const std::string& name) {
    return name.find("eye") != std::string::npos ||
           name.find("Eye") != std::string::npos ||
           name.find("eyes") != std::string::npos;
}

} // namespace

bool PsdAssembler::resolveSceneFiles(
    const std::string& sceneId,
    SceneFiles& out,
    std::string& error
) {
    if (sceneId.empty()) {
        error = "Scene id is empty.";
        return false;
    }

    out.baseDir = "assets/scenes/" + sceneId;
    out.psdPath = out.baseDir + "/" + sceneId + ".psd";
    out.configPath = out.baseDir + "/" + sceneId + ".json";
    return true;
}

bool PsdAssembler::getDialogueAnchorNormalized(
    const PsdAssembly& assembly,
    int sceneWidth,
    int sceneHeight,
    Vec2& outNormalized
) {
    outNormalized = Vec2{ 0.5f, 0.5f };

    if (sceneWidth <= 0 || sceneHeight <= 0) {
        return false;
    }

    cpVect world;
    if (!getDialogueAnchorWorldPosition(assembly.dialogueAnchor, world)) {
        return false;
    }

    outNormalized.x = static_cast<float>(world.x) / static_cast<float>(sceneWidth);
    outNormalized.y = static_cast<float>(world.y) / static_cast<float>(sceneHeight);

    outNormalized.x = std::clamp(outNormalized.x, 0.0f, 1.0f);
    outNormalized.y = std::clamp(outNormalized.y, 0.0f, 1.0f);
    return true;
}

bool PsdAssembly::setItemVisible(const std::string& name, bool visible) {
    auto it = renderRegistry.find(name);
    if (it == renderRegistry.end()) {
        return false;
    }

    RenderItem& ri = it->second;
    if (!ri.part) {
        return false;
    }

    if (ri.kind == RenderItemKind::Part) {
        ri.part->visible = visible;
        return true;
    }

    if (ri.kind == RenderItemKind::SoftOverlay) {
        if (ri.part->kind != PartKind::Soft) {
            return false;
        }

        if (ri.overlayIndex < 0 ||
            ri.overlayIndex >= static_cast<int>(ri.part->overlays.size())) {
            return false;
        }

        ri.part->overlays[ri.overlayIndex].visible = visible;
        return true;
    }

    return false;
}

bool PsdAssembly::isItemVisible(const std::string& name) const {
    auto it = renderRegistry.find(name);
    if (it == renderRegistry.end()) {
        return false;
    }

    const RenderItem& ri = it->second;
    if (!ri.part) {
        return false;
    }

    if (ri.kind == RenderItemKind::Part) {
        return ri.part->visible;
    }

    if (ri.kind == RenderItemKind::SoftOverlay) {
        if (ri.part->kind != PartKind::Soft) {
            return false;
        }

        if (ri.overlayIndex < 0 ||
            ri.overlayIndex >= static_cast<int>(ri.part->overlays.size())) {
            return false;
        }

        return ri.part->overlays[ri.overlayIndex].visible;
    }

    return false;
}

void PsdAssembly::setOnlyVisible(
    const std::vector<std::string>& namesToHideShow,
    const std::string& visibleName
) {
    for (const std::string& name : namesToHideShow) {
        setItemVisible(name, name == visibleName);
    }
}

bool PsdAssembler::buildScene(
    cpSpace* space,
    render::GameRenderer& renderer,
    const std::string& sceneId,
    int sceneWidth,
    int sceneHeight,
    PsdAssembly& out,
    std::string& error
) {
    destroyAssembly(space, renderer, out);

    SceneFiles files;
    if (!resolveSceneFiles(sceneId, files, error)) {
        return false;
    }

    SceneConfig config;
    if (!SceneConfig::loadFromFile(files.configPath, config, error)) {
        return false;
    }

    Psd psd;
    if (!PsdLoader::LoadPsd(files.psdPath, psd, error)) {
        return false;
    }

    out.sceneId = sceneId;
    out.psdPath = files.psdPath;
    out.configPath = files.configPath;

    const Vec2 canvasCenter((float)psd.canvasWidth * 0.5f, (float)psd.canvasHeight * 0.5f);
    const Vec2 sceneWorldPos = resolveSceneWorldPosition(config.scene, sceneWidth, sceneHeight);
    const float sceneScale = config.scene.scale;

    std::unordered_map<std::string, std::vector<const LayerImageRGBA*>> overlaysByBase;
    for (auto& kv : psd.layersByName) {
        const LayerImageRGBA& layer = kv.second;
        std::string base;
        if (parseOverlayName(layer.name, base)) {
            overlaysByBase[base].push_back(&layer);
        }
    }

    out.parts.reserve(psd.layersByName.size());

    for (auto& kv : psd.layersByName) {
        const LayerImageRGBA& layer = kv.second;

        std::string base;
        if (parseOverlayName(layer.name, base)) {
            continue;
        }

        RenderPart part;
        part.name = layer.name;

        const ImageRGBA image = makeImageFromLayer(layer);

        auto softIt = config.softOverrides.find(layer.name);
        if (softIt == config.softOverrides.end()) {
            part.kind = PartKind::Rigid;

            RigidBuildParams rigidParams;
            rigidParams.density = 0.0005f;
            rigidParams.friction = 0.1f;
            rigidParams.elasticity = 0.05f;

            part.rigid = PhysicsBodyFactory::createRigidBodyFromImage(
                space,
                layer.name,
                image,
                canvasCenter,
                sceneWorldPos,
                sceneScale,
                rigidParams
            );

            part.anchorCanvas = part.rigid.anchorCanvas;
            part.render.texW = layer.width;
            part.render.texH = layer.height;
            part.render.tex = renderer.uploadTextureRGBA(layer.rgba.data(), layer.width, layer.height);
            part.render.halfW = part.rigid.halfWidthWorld;
            part.render.halfH = part.rigid.halfHeightWorld;

            const bool is_eye = isEyeLayerName(part.name);
            part.emissive = is_eye;
            part.unlit = false;

            const auto& ob = part.rigid.opaqueBounds;
            part.render.u0 = float(ob.minx) / float(layer.width);
            part.render.v0 = float(ob.miny) / float(layer.height);
            part.render.u1 = float(ob.maxx + 1) / float(layer.width);
            part.render.v1 = float(ob.maxy + 1) / float(layer.height);
        } else {
            part.kind = PartKind::Soft;

            SceneSoftOverride sceneSoft = softIt->second;
            SoftBuildParams softParams = sceneSoft.params;
            softParams.scale = sceneScale;

            softParams.attachWorld = canvasToWorldCp(
                sceneSoft.attachCanvas,
                canvasCenter,
                sceneWorldPos,
                softParams.scale
            );
            softParams.attachRadiusWorld = sceneSoft.attachRadiusPx * softParams.scale;

            SoftPinnedBuildParams pinnedBuildParams;
            pinnedBuildParams.vertex.enabled = sceneSoft.pinnedParams.enabled;
            pinnedBuildParams.vertex.overrideMassPerPoint = sceneSoft.pinnedParams.overrideMassPerPoint;
            pinnedBuildParams.vertex.massPerPoint = sceneSoft.pinnedParams.massPerPoint;
            pinnedBuildParams.vertex.overrideRadius = sceneSoft.pinnedParams.overrideRadius;
            pinnedBuildParams.vertex.radius = sceneSoft.pinnedParams.radius;
            pinnedBuildParams.vertex.overrideFriction = sceneSoft.pinnedParams.overrideFriction;
            pinnedBuildParams.vertex.friction = sceneSoft.pinnedParams.friction;
            pinnedBuildParams.vertex.overrideElasticity = sceneSoft.pinnedParams.overrideElasticity;
            pinnedBuildParams.vertex.elasticity = sceneSoft.pinnedParams.elasticity;

            pinnedBuildParams.constraint.maxForce = sceneSoft.pinnedConstraint.maxForce;
            pinnedBuildParams.constraint.errorBias = sceneSoft.pinnedConstraint.errorBias;
            pinnedBuildParams.constraint.maxBias = sceneSoft.pinnedConstraint.maxBias;

            if (sceneSoft.useMeshJson) {
                MeshData mesh;
                std::string meshError;
                std::string meshPath = files.baseDir + "/" + sceneSoft.meshJsonPath;

                if (loadMeshJson(meshPath, mesh, meshError)) {
                    part.soft = PhysicsBodyFactory::createSoftBodyFromMesh(
                        space,
                        layer.name,
                        mesh,
                        canvasCenter,
                        sceneWorldPos,
                        softParams,
                        &pinnedBuildParams
                    );
                } else {
                    std::cerr << "[soft] " << meshError << " Falling back to alpha mesh for " << layer.name << "\n";

                    const Vec2 centroid = computeOpaqueCentroid(image, 10);
                    const Vec2 worldCenter = canvasToWorld(centroid, canvasCenter, sceneWorldPos, softParams.scale);

                    part.soft = PhysicsBodyFactory::createSoftBodyFromAlpha(
                        space,
                        layer.name,
                        image,
                        canvasCenter,
                        worldCenter,
                        softParams,
                        &pinnedBuildParams
                    );
                }
            } else {
                const Vec2 centroid = computeOpaqueCentroid(image, 10);
                const Vec2 worldCenter = canvasToWorld(centroid, canvasCenter, sceneWorldPos, softParams.scale);

                part.soft = PhysicsBodyFactory::createSoftBodyFromAlpha(
                    space,
                    layer.name,
                    image,
                    canvasCenter,
                    worldCenter,
                    softParams,
                    &pinnedBuildParams
                );
            }

            part.anchorCanvas = part.soft.anchorCanvas;
            part.render.texW = layer.width;
            part.render.texH = layer.height;
            part.render.tex = renderer.uploadTextureRGBA(layer.rgba.data(), layer.width, layer.height);

            const bool is_eye = isEyeLayerName(part.name);
            part.emissive = is_eye;
            part.unlit = false;

            std::string softRenderError;
            if (!renderer.initializeSoftRenderMesh(part, softRenderError)) {
                error = "Failed to initialize soft render mesh for " + layer.name + ": " + softRenderError;
                destroyAssembly(space, renderer, out);
                return false;
            }
        }

        out.parts.push_back(std::move(part));
    }

    out.partsByName.reserve(out.parts.size());
    for (RenderPart& p : out.parts) {
        out.partsByName[p.name] = &p;
    }

    if (config.dialogueAnchor.enabled) {
        if (!initializeDialogueAnchorRuntime(
                config.dialogueAnchor,
                out.partsByName,
                canvasCenter,
                sceneWorldPos,
                sceneScale,
                out.dialogueAnchor)) {
            std::cerr << "[dialogue_anchor] failed to initialize for scene: " << sceneId << "\n";
        }
    }

    for (auto& pr : overlaysByBase) {
        auto itPart = out.partsByName.find(pr.first);
        if (itPart == out.partsByName.end()) {
            continue;
        }

        RenderPart* basePart = itPart->second;
        if (basePart->kind != PartKind::Soft) {
            continue;
        }

        for (const LayerImageRGBA* ovLayer : pr.second) {
            SoftOverlay ov;
            ov.name = ovLayer->name;
            ov.tex = renderer.uploadTextureRGBA(ovLayer->rgba.data(), ovLayer->width, ovLayer->height);
            const bool is_eye = isEyeLayerName(ov.name);
            ov.emissive = is_eye;
            ov.unlit = is_eye;
            basePart->overlays.push_back(std::move(ov));
        }
    }

    out.collisionRules = std::make_unique<CollisionRules>();
    {
        std::uint32_t nextId = 1;
        for (auto& kv : out.partsByName) {
            out.collisionRules->partId[kv.first] = nextId++;
        }

        for (const auto& pair : config.collisionRules.allowPairs) {
            auto itA = out.collisionRules->partId.find(pair.first);
            auto itB = out.collisionRules->partId.find(pair.second);
            if (itA == out.collisionRules->partId.end() || itB == out.collisionRules->partId.end()) {
                std::cerr << "[collision] unknown part in allow-list: " << pair.first << " / " << pair.second << "\n";
                continue;
            }

            out.collisionRules->allowedPairs.insert(pairKey(itA->second, itB->second));
        }

        cpSpaceSetUserData(space, out.collisionRules.get());
        auto* handler = cpSpaceAddDefaultCollisionHandler(space);
        handler->beginFunc = allowListBegin;

        cpGroup nextGroup = 1;
        for (RenderPart& p : out.parts) {
            p.collisionId = out.collisionRules->partId[p.name];
            p.collisionGroup = nextGroup++;

            if (p.kind == PartKind::Rigid) {
                cpShapeSetFilter(
                    p.rigid.shape,
                    cpShapeFilterNew(p.collisionGroup, CP_ALL_CATEGORIES, CP_ALL_CATEGORIES)
                );

                auto* tag = new ShapeTag();
                tag->partId = p.collisionId;
                tag->debugName = p.name.c_str();
                cpShapeSetUserData(p.rigid.shape, tag);
            } else {
                for (cpShape* sh : p.soft.body.shapes) {
                    cpShapeSetFilter(
                        sh,
                        cpShapeFilterNew(p.collisionGroup, CP_ALL_CATEGORIES, CP_ALL_CATEGORIES)
                    );

                    auto* tag = new ShapeTag();
                    tag->partId = p.collisionId;
                    tag->debugName = p.name.c_str();
                    cpShapeSetUserData(sh, tag);
                }
            }
        }
    }

    out.joints.clear();
    out.joints.reserve(config.joints.size());
    for (const SceneJointConfig& jointCfg : config.joints) {
        out.joints.push_back(connectPartsPivotLimited(
            space,
            out.partsByName,
            jointCfg,
            canvasCenter,
            sceneWorldPos,
            sceneScale
        ));
    }

    buildRenderRegistry(out.parts, out.renderRegistry);
    updateRenderOrderItems(out.parts, out.renderRegistry, config.renderOrder, out.renderItems);

    return true;
}

bool PsdAssembler::rebuildScene(
    cpSpace* space,
    render::GameRenderer& renderer,
    const std::string& sceneId,
    int sceneWidth,
    int sceneHeight,
    PsdAssembly& out,
    std::string& error
) {
    return buildScene(space, renderer, sceneId, sceneWidth, sceneHeight, out, error);
}

void PsdAssembler::destroyAssembly(
    cpSpace* space,
    render::GameRenderer& renderer,
    PsdAssembly& assembly
) {
    for (JointPair& jp : assembly.joints) {
        if (jp.pivot)      { cpSpaceRemoveConstraint(space, jp.pivot);      cpConstraintFree(jp.pivot);      jp.pivot = nullptr; }
        if (jp.limit)      { cpSpaceRemoveConstraint(space, jp.limit);      cpConstraintFree(jp.limit);      jp.limit = nullptr; }
        if (jp.framePivot) { cpSpaceRemoveConstraint(space, jp.framePivot); cpConstraintFree(jp.framePivot); jp.framePivot = nullptr; }
        if (jp.rotSpring)  { cpSpaceRemoveConstraint(space, jp.rotSpring);  cpConstraintFree(jp.rotSpring);  jp.rotSpring = nullptr; }
    }
    assembly.joints.clear();

    for (RenderPart& part : assembly.parts) {
        if (part.kind == PartKind::Soft) {
            for (cpShape* s : part.soft.body.shapes) {
                if (auto* tag = reinterpret_cast<ShapeTag*>(cpShapeGetUserData(s))) {
                    delete tag;
                    cpShapeSetUserData(s, nullptr);
                }
            }
        } else {
            if (part.rigid.shape) {
                if (auto* tag = reinterpret_cast<ShapeTag*>(cpShapeGetUserData(part.rigid.shape))) {
                    delete tag;
                    cpShapeSetUserData(part.rigid.shape, nullptr);
                }
            }
        }

        for (auto& ov : part.overlays) {
            if (ov.tex) {
                glDeleteTextures(1, &ov.tex);
                ov.tex = 0;
            }
        }
        part.overlays.clear();

        if (part.render.tex) {
            glDeleteTextures(1, &part.render.tex);
            part.render.tex = 0;
        }

        if (part.kind == PartKind::Soft) {
            renderer.destroySoftRenderMesh(part);
            PhysicsBodyFactory::destroySoftBody(space, part.soft);
        } else {
            PhysicsBodyFactory::destroyRigidBody(space, part.rigid);
        }
    }

    assembly.parts.clear();
    assembly.partsByName.clear();
    assembly.renderRegistry.clear();
    assembly.renderItems.clear();
    assembly.sceneId.clear();
    assembly.psdPath.clear();
    assembly.configPath.clear();

    cpSpaceSetUserData(space, nullptr);
    assembly.collisionRules.reset();
}
} // namespace physics