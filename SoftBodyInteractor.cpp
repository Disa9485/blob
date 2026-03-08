// SoftBodyInteractor.cpp
#include "SoftBodyInteractor.hpp"
#include "PhysicsScene.hpp"

#include <cctype>
#include <limits>
#include <sstream>

namespace physics {

SoftBodyInteractor::~SoftBodyInteractor() {
    // caller should already have cleaned up, but keep this harmless
}

void SoftBodyInteractor::shutdown(cpSpace* space) {
    endDrag(space);
    grabbedPart_ = nullptr;
    grabbedBody_ = nullptr;
    grabbedLocalAnchor_ = cpvzero;
    clearTouchedParts();
}

static int buildPartRenderPriority(
    const PsdAssembly& assembly,
    const RenderPart* targetPart
) {
    int bestPriority = -1;

    for (int i = 0; i < static_cast<int>(assembly.renderItems.size()); ++i) {
        const RenderItem& item = assembly.renderItems[i];
        if (item.part == targetPart) {
            bestPriority = i;
        }
    }

    return bestPriority;
}

SoftBodyInteractor::PickResult SoftBodyInteractor::pickNearestSoftBodyPoint(
    PsdAssembly& assembly,
    cpVect mouseWorld
) const {
    PickResult best;
    best.renderPriority = -1;

    const cpFloat maxDist2 = cpFloat(config_.pickRadiusPx * config_.pickRadiusPx);

    std::unordered_map<const RenderPart*, int> renderPriorityByPart;
    renderPriorityByPart.reserve(assembly.parts.size());

    for (int i = 0; i < static_cast<int>(assembly.renderItems.size()); ++i) {
        const RenderItem& item = assembly.renderItems[i];
        if (item.part && item.part->kind == PartKind::Soft) {
            renderPriorityByPart[item.part] = i;
        }
    }

    for (RenderPart& part : assembly.parts) {
        if (part.kind != PartKind::Soft) {
            continue;
        }

        int renderPriority = -1;
        auto prIt = renderPriorityByPart.find(&part);
        if (prIt != renderPriorityByPart.end()) {
            renderPriority = prIt->second;
        }

        for (cpBody* body : part.soft.body.bodies) {
            if (!body) {
                continue;
            }

            const cpVect p = cpBodyGetPosition(body);
            const cpFloat d2 = cpvlengthsq(cpvsub(p, mouseWorld));

            if (d2 > maxDist2) {
                continue;
            }

            if (renderPriority > best.renderPriority ||
                (renderPriority == best.renderPriority && d2 < best.dist2)) {
                best.part = &part;
                best.body = body;
                best.dist2 = d2;
                best.renderPriority = renderPriority;
            }
        }
    }

    return best;
}

std::string SoftBodyInteractor::humanizePartId(const std::string& id) {
    std::string out;
    out.reserve(id.size());

    bool capitalizeNext = true;
    for (char ch : id) {
        if (ch == '_') {
            out.push_back(' ');
            capitalizeNext = true;
            continue;
        }

        unsigned char uch = static_cast<unsigned char>(ch);
        if (capitalizeNext) {
            out.push_back(static_cast<char>(std::toupper(uch)));
            capitalizeNext = false;
        } else {
            out.push_back(static_cast<char>(std::tolower(uch)));
        }
    }

    return out;
}

void SoftBodyInteractor::recordTouchedPart(const std::string& partId) {
    const std::string humanReadable = humanizePartId(partId);
    if (touchedPartsSeen_.insert(humanReadable).second) {
        touchedPartsHumanReadable_.push_back(humanReadable);
    }
}

void SoftBodyInteractor::clearTouchedParts() {
    touchedPartsHumanReadable_.clear();
    touchedPartsSeen_.clear();
}

std::string SoftBodyInteractor::consumeTouchedPartsSentence(const std::string& userName) {
    if (touchedPartsHumanReadable_.empty()) {
        return "";
    }

    std::ostringstream out;
    out << userName << " grabbed your ";

    if (touchedPartsHumanReadable_.size() == 1) {
        out << touchedPartsHumanReadable_[0] << ".";
    } else if (touchedPartsHumanReadable_.size() == 2) {
        out << touchedPartsHumanReadable_[0]
            << " and "
            << touchedPartsHumanReadable_[1]
            << ".";
    } else {
        for (std::size_t i = 0; i < touchedPartsHumanReadable_.size(); ++i) {
            if (i == touchedPartsHumanReadable_.size() - 1) {
                out << "and " << touchedPartsHumanReadable_[i] << ".";
            } else {
                out << touchedPartsHumanReadable_[i] << ", ";
            }
        }
    }

    const std::string result = out.str();
    clearTouchedParts();
    return result;
}

void SoftBodyInteractor::beginDrag(
    PhysicsScene& scene,
    PsdAssembly& assembly,
    float mouseX,
    float mouseY
) {
    cpSpace* space = scene.space();
    cpBody* mouseBody = scene.mouseBody();
    if (!space || !mouseBody) {
        return;
    }

    endDrag(space);

    const cpVect mouseWorld = cpv(mouseX, mouseY);
    const PickResult pick = pickNearestSoftBodyPoint(assembly, mouseWorld);
    if (!pick.body || !pick.part) {
        return;
    }

    grabbedPart_ = pick.part;
    grabbedBody_ = pick.body;

    if (!grabbedPart_->name.empty()) {
        recordTouchedPart(grabbedPart_->name);
    }

    cpBodySetPosition(mouseBody, mouseWorld);
    cpBodySetVelocity(mouseBody, cpvzero);
    cpBodySetAngularVelocity(mouseBody, 0.0);

    grabbedLocalAnchor_ = cpBodyWorldToLocal(grabbedBody_, mouseWorld);

    grabSpring_ = cpDampedSpringNew(
        mouseBody,
        grabbedBody_,
        cpvzero,
        grabbedLocalAnchor_,
        0.0,
        config_.springStiffness,
        config_.springDamping
    );

    cpConstraintSetMaxForce(grabSpring_, config_.maxForce);
    cpConstraintSetErrorBias(grabSpring_, config_.errorBias);

    cpSpaceAddConstraint(space, grabSpring_);
}

void SoftBodyInteractor::updateDrag(
    PhysicsScene& scene,
    float mouseX,
    float mouseY
) {
    if (!grabSpring_) {
        return;
    }

    cpBody* mouseBody = scene.mouseBody();
    if (!mouseBody) {
        return;
    }

    cpBodySetPosition(mouseBody, cpv(mouseX, mouseY));
    cpBodySetVelocity(mouseBody, cpvzero);
    cpBodySetAngularVelocity(mouseBody, 0.0);
}

void SoftBodyInteractor::endDrag(cpSpace* space) {
    if (grabSpring_ && space) {
        cpSpaceRemoveConstraint(space, grabSpring_);
        cpConstraintFree(grabSpring_);
    }

    grabSpring_ = nullptr;
    grabbedPart_ = nullptr;
    grabbedBody_ = nullptr;
    grabbedLocalAnchor_ = cpvzero;
}

} // namespace physics