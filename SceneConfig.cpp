#include "SceneConfig.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace physics {
namespace {

using json = nlohmann::json;

static bool readVec2(const json& j, Vec2& out, const char* fieldName, std::string& error) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
        error = std::string("Expected [x, y] for field: ") + fieldName;
        return false;
    }

    out.x = j[0].get<float>();
    out.y = j[1].get<float>();
    return true;
}

static void loadPinnedParams(const json& j, ScenePinnedParams& out) {
    out.enabled = true;

    if (j.contains("mass_per_point")) {
        out.overrideMassPerPoint = true;
        out.massPerPoint = j.at("mass_per_point").get<float>();
    }

    if (j.contains("radius")) {
        out.overrideRadius = true;
        out.radius = j.at("radius").get<float>();
    }

    if (j.contains("friction")) {
        out.overrideFriction = true;
        out.friction = j.at("friction").get<float>();
    }

    if (j.contains("elasticity")) {
        out.overrideElasticity = true;
        out.elasticity = j.at("elasticity").get<float>();
    }
}

static void loadPinnedConstraintParams(const json& j, ScenePinnedConstraintConfig& out) {
    out.maxForce = j.value("max_force", out.maxForce);
    out.errorBias = j.value("error_bias", out.errorBias);
    out.maxBias = j.value("max_bias", out.maxBias);
}

static void loadSoftBuildParams(const json& j, SoftBuildParams& out) {
    out.pointSpacing = j.value("point_spacing", out.pointSpacing);
    out.massPerPoint = j.value("mass_per_point", out.massPerPoint);
    out.radius = j.value("radius", out.radius);
    out.springK = j.value("spring_k", out.springK);
    out.springDamping = j.value("spring_damping", out.springDamping);
    out.friction = j.value("friction", out.friction);
    out.elasticity = j.value("elasticity", out.elasticity);
    out.stretchMinRatio = j.value("stretch_min_ratio", out.stretchMinRatio);
    out.stretchMaxRatio = j.value("stretch_max_ratio", out.stretchMaxRatio);
    out.perimeterBendKRatio = j.value("perimeter_bend_k_ratio", out.perimeterBendKRatio);
    out.centerSpringKRatio = j.value("center_spring_k_ratio", out.centerSpringKRatio);
}

} // namespace

bool SceneConfig::loadFromFile(
    const std::string& path,
    SceneConfig& out,
    std::string& error
) {
    out = SceneConfig{};

    std::ifstream file(path);
    if (!file.is_open()) {
        error = "Failed to open scene config: " + path;
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const std::exception& e) {
        error = std::string("Failed to parse scene config '") + path + "': " + e.what();
        return false;
    }

    try {
        if (root.contains("scene")) {
            const json& scene = root.at("scene");

            out.scene.scale = scene.value("scale", out.scene.scale);
            out.scene.walls = scene.value("walls", out.scene.walls);

            if (scene.contains("spawn_position")) {
                if (!readVec2(scene.at("spawn_position"), out.scene.spawnPosition, "scene.spawn_position", error)) {
                    return false;
                }
            }
        }

        if (root.contains("soft_overrides")) {
            const json& softOverrides = root.at("soft_overrides");
            if (!softOverrides.is_object()) {
                error = "soft_overrides must be an object.";
                return false;
            }

            for (auto it = softOverrides.begin(); it != softOverrides.end(); ++it) {
                const std::string& partName = it.key();
                const json& sj = it.value();

                SceneSoftOverride ov;

                if (!sj.contains("attach_canvas")) {
                    error = "soft_overrides." + partName + " is missing attach_canvas.";
                    return false;
                }

                if (!readVec2(sj.at("attach_canvas"), ov.attachCanvas, ("soft_overrides." + partName + ".attach_canvas").c_str(), error)) {
                    return false;
                }

                ov.attachRadiusPx = sj.value("attach_radius_px", 0.0f);
                ov.useMeshJson = sj.value("use_mesh_json", false);
                ov.meshJsonPath = sj.value("mesh_json", std::string{});

                if (sj.contains("params")) {
                    if (!sj.at("params").is_object()) {
                        error = "soft_overrides." + partName + ".params must be an object.";
                        return false;
                    }
                    loadSoftBuildParams(sj.at("params"), ov.params);
                }

                if (sj.contains("pinned_params")) {
                    if (!sj.at("pinned_params").is_object()) {
                        error = "soft_overrides." + partName + ".pinned_params must be an object.";
                        return false;
                    }
                    loadPinnedParams(sj.at("pinned_params"), ov.pinnedParams);
                }

                if (sj.contains("pinned_constraint")) {
                    if (!sj.at("pinned_constraint").is_object()) {
                        error = "soft_overrides." + partName + ".pinned_constraint must be an object.";
                        return false;
                    }
                    loadPinnedConstraintParams(sj.at("pinned_constraint"), ov.pinnedConstraint);
                }

                out.softOverrides[partName] = ov;
            }
        }

        if (root.contains("collision_rules")) {
            const json& rules = root.at("collision_rules");
            if (!rules.is_object()) {
                error = "collision_rules must be an object.";
                return false;
            }

            if (rules.contains("allow")) {
                const json& allow = rules.at("allow");
                if (!allow.is_array()) {
                    error = "collision_rules.allow must be an array.";
                    return false;
                }

                for (std::size_t i = 0; i < allow.size(); ++i) {
                    const json& pair = allow.at(i);
                    if (!pair.is_array() || pair.size() != 2 || !pair[0].is_string() || !pair[1].is_string()) {
                        error = "collision_rules.allow entries must be [\"partA\", \"partB\"].";
                        return false;
                    }

                    out.collisionRules.allowPairs.emplace_back(
                        pair[0].get<std::string>(),
                        pair[1].get<std::string>()
                    );
                }
            }
        }

        if (root.contains("render_order")) {
            const json& ro = root.at("render_order");
            if (!ro.is_array()) {
                error = "render_order must be an array.";
                return false;
            }

            for (const auto& item : ro) {
                if (!item.is_string()) {
                    error = "render_order entries must be strings.";
                    return false;
                }
                out.renderOrder.push_back(item.get<std::string>());
            }
        }

        if (root.contains("joints")) {
            const json& joints = root.at("joints");
            if (!joints.is_array()) {
                error = "joints must be an array.";
                return false;
            }

            for (std::size_t i = 0; i < joints.size(); ++i) {
                const json& jj = joints.at(i);
                if (!jj.is_object()) {
                    error = "Each joints entry must be an object.";
                    return false;
                }

                SceneJointConfig joint;
                joint.child = jj.value("child", std::string{});
                joint.parent = jj.value("parent", std::string{});

                if (joint.child.empty() || joint.parent.empty()) {
                    error = "Each joint must define non-empty child and parent.";
                    return false;
                }

                if (!jj.contains("anchor_canvas")) {
                    error = "Joint '" + joint.child + "' -> '" + joint.parent + "' is missing anchor_canvas.";
                    return false;
                }

                if (!readVec2(jj.at("anchor_canvas"), joint.anchorCanvas, "joints[].anchor_canvas", error)) {
                    return false;
                }

                joint.minDeg = jj.value("min_deg", 0.0f);
                joint.maxDeg = jj.value("max_deg", 0.0f);
                joint.maxForcePivot = jj.value("max_force_pivot", joint.maxForcePivot);
                joint.maxForceLimit = jj.value("max_force_limit", joint.maxForceLimit);
                joint.enableRotSpring = jj.value("enable_rot_spring", false);
                joint.rotSpringK = jj.value("rot_spring_k", (cpFloat)0.0f);
                joint.rotSpringDamp = jj.value("rot_spring_damp", (cpFloat)0.0f);

                if (jj.contains("rest_angle_rad_override") && jj.at("rest_angle_rad_override").is_number()) {
                    joint.restAngleRadOverride = jj.at("rest_angle_rad_override").get<cpFloat>();
                }

                out.joints.push_back(joint);
            }
        }
    } catch (const std::exception& e) {
        error = std::string("Invalid scene config '") + path + "': " + e.what();
        return false;
    }

    return true;
}

} // namespace physics