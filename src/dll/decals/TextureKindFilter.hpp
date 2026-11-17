#pragma once
#include <chrono>

#include "imgui.h"
#include "DecalRepository.hpp"

// Base, Overlay and Sidewalk checkboxes for a texture grid. Textures are sorted into base and
// overlay only once one is unticked, a few each frame.
struct TextureKindFilter {
    bool base = true;
    bool overlay = true;
    bool sidewalk = true;

    [[nodiscard]] bool Active() const { return !(base && overlay && sidewalk); }

    // Textures not sorted yet only show while both are ticked.
    [[nodiscard]] bool Allows(const DecalRepository& decals, const uint32_t instanceId) const {
        if (!Active()) {
            return true;
        }
        const decals::TextureKind kind = decals.KindOf(instanceId);
        switch (kind) {
        case decals::TextureKind::Base:
            return base;
        case decals::TextureKind::Overlay:
            return overlay;
        case decals::TextureKind::Sidewalk:
            return sidewalk;
        default:
            return false;
        }
    }

    // The checkboxes on the current line, then a line on the sorting while it goes on. True when
    // what Allows says may have changed.
    bool Render(DecalRepository& decals) {
        const size_t before = decals.ClassifiedCount();
        bool changed = ImGui::Checkbox("Base", &base);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show solid textures. These fully cover the ground.");
        }
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Overlay", &overlay);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show textures with transparent parts. These go on top of other textures, for example markings and dirt.");
        }
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Sidewalk", &sidewalk);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show the sidewalks that the game puts along roads.");
        }
        if (!(base && overlay) && !decals.ClassifySome(std::chrono::milliseconds(6))) {
            ImGui::TextDisabled("Textures sorted into base and overlay: %zu of %zu", decals.ClassifiedCount(),
                                decals.Count());
        }
        return changed || decals.ClassifiedCount() != before;
    }
};
