#pragma once

#include <algorithm>
#include <cstdint>

#include "DecalTextureLoader.hpp"
#include "imgui.h"
#include "public/ImGuiTexture.h"

// The large picture of a texture in the tooltip of a grid tile. Holds one texture, the last hovered.
class TextureHoverPreview {
public:
    void Clear() {
        texture_.Release();
        instanceId_ = 0;
        size_ = ImVec2(0.0f, 0.0f);
    }

    // Inside a tooltip. Loads the texture when another one is hovered.
    void Render(cIGZPersistResourceManager* pRM, cIGZImGuiService* imguiService, const uint32_t instanceId) {
        constexpr float kMinSide = 128.0f;
        constexpr float kMaxSide = 512.0f;

        if (instanceId != instanceId_) {
            Clear();
            instanceId_ = instanceId;  // also on failure: no new attempt each frame
            decals::LoadDecalTexture(pRM, imguiService, instanceId, texture_, size_);
        }
        if (!texture_.GetID() || size_.x <= 0.0f || size_.y <= 0.0f) {
            return;
        }

        const float longest = std::max(size_.x, size_.y);
        const float scale = std::clamp(longest, kMinSide, kMaxSide) / longest;
        ImGui::Image(reinterpret_cast<ImTextureID>(texture_.GetID()), ImVec2(size_.x * scale, size_.y * scale));
    }

private:
    ImGuiTexture texture_{};
    uint32_t instanceId_{0};
    ImVec2 size_{0.0f, 0.0f};
};
