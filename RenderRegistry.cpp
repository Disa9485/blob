// RenderRegistry.cpp
#include "RenderRegistry.hpp"

#include <iostream>
#include <unordered_set>

namespace physics {

void buildRenderRegistry(
    const std::vector<RenderPart>& parts,
    std::unordered_map<std::string, RenderItem>& out
) {
    out.clear();
    out.reserve(parts.size() * 2);

    for (const RenderPart& p : parts) {
        RenderItem ri;
        ri.kind = RenderItemKind::Part;
        ri.part = &p;
        out[p.name] = ri;
    }

    for (const RenderPart& p : parts) {
        if (p.kind != PartKind::Soft) {
            continue;
        }

        for (int i = 0; i < static_cast<int>(p.overlays.size()); ++i) {
            RenderItem ri;
            ri.kind = RenderItemKind::SoftOverlay;
            ri.part = &p;
            ri.overlayIndex = i;
            out[p.overlays[i].name] = ri;
        }
    }
}

void updateRenderOrderItems(
    const std::vector<RenderPart>& parts,
    const std::unordered_map<std::string, RenderItem>& registry,
    const std::vector<std::string>& orderedNames,
    std::vector<RenderItem>& out,
    bool warnOnMissing
) {
    out.clear();
    out.reserve(parts.size());

    std::unordered_set<const void*> used;
    used.reserve(parts.size() * 2);

    auto markKey = [&](const RenderItem& ri) -> const void* {
        if (ri.kind == RenderItemKind::Part) {
            return static_cast<const void*>(ri.part);
        }
        uintptr_t v = reinterpret_cast<uintptr_t>(ri.part);
        v ^= static_cast<uintptr_t>(ri.overlayIndex + 1) * 0x9e3779b97f4a7c15ULL;
        return reinterpret_cast<const void*>(v);
    };

    for (const std::string& name : orderedNames) {
        auto it = registry.find(name);
        if (it == registry.end()) {
            if (warnOnMissing) {
                std::cerr << "[render order] missing item: " << name << "\n";
            }
            continue;
        }

        const RenderItem& ri = it->second;
        const void* key = markKey(ri);
        if (used.insert(key).second) {
            out.push_back(ri);
        }
    }

    for (const RenderPart& p : parts) {
        auto it = registry.find(p.name);
        if (it == registry.end()) {
            continue;
        }

        const RenderItem& ri = it->second;
        const void* key = markKey(ri);
        if (used.insert(key).second) {
            out.push_back(ri);
        }
    }
}

} // namespace physics