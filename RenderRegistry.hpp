// RenderRegistry.hpp
#pragma once

#include "PhysicsRuntimeTypes.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace physics {

void buildRenderRegistry(
    const std::vector<RenderPart>& parts,
    std::unordered_map<std::string, RenderItem>& out
);

void updateRenderOrderItems(
    const std::vector<RenderPart>& parts,
    const std::unordered_map<std::string, RenderItem>& registry,
    const std::vector<std::string>& orderedNames,
    std::vector<RenderItem>& out,
    bool warnOnMissing = true
);

} // namespace physics