#pragma once
#include <chrono>

#include "imgui.h"
#include "DecalRepository.hpp"

// Base and Overlay checkboxes for a texture grid. Textures are sorted into the two only once one
// is unticked, a few each frame.
struct TextureKindFilter {
    bool base = true;
    bool overlay = true;

    [[nodiscard]] bool Active() const { return !(base && overlay); }

    // Textures not sorted yet only show while both are ticked.
    [[nodiscard]] bool Allows(const DecalRepository& decals, const uint32_t instanceId) const {
        if (!Active()) {
            return true;
        }
        const decals::TextureKind kind = decals.KindOf(instanceId);
        return kind == decals::TextureKind::Base ? base : kind == decals::TextureKind::Overlay && overlay;
    }

    // The checkboxes on the current line, then a line on the sorting while it goes on. True when
    // what Allows says may have changed.
    bool Render(DecalRepository& decals) {
        const size_t before = decals.ClassifiedCount();
        bool changed = ImGui::Checkbox("Base", &base);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Opaque textures, that cover the ground.");
        }
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Overlay", &overlay);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Textures with transparent parts, that lie over others: markings, cracks, dirt.");
        }
        if (Active() && !decals.ClassifySome(std::chrono::milliseconds(6))) {
            ImGui::TextDisabled("Sorting textures into base and overlay: %zu of %zu", decals.ClassifiedCount(),
                                decals.Count());
        }
        return changed || decals.ClassifiedCount() != before;
    }
};
