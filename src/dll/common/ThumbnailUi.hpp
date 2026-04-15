#pragma once

#include <optional>

#include "imgui.h"

namespace ThumbnailUi {
    constexpr float kCornerRounding = 4.0f;
    constexpr float kBorderThickness = 1.0f;

    inline void Render(const std::optional<void*> textureId,
                       const ImVec2 size,
                       const ImU32 backgroundColor,
                       const ImU32 borderColor) {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max{min.x + size.x, min.y + size.y};
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        ImGui::Dummy(size);

        if ((backgroundColor >> IM_COL32_A_SHIFT) != 0) {
            drawList->AddRectFilled(min, max, backgroundColor, kCornerRounding);
        }

        if (textureId.has_value() && *textureId != nullptr) {
            drawList->AddImageRounded(
                reinterpret_cast<ImTextureID>(*textureId),
                min,
                max,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                IM_COL32_WHITE,
                kCornerRounding);
        }

        if ((borderColor >> IM_COL32_A_SHIFT) != 0) {
            const ImVec2 borderMax{max.x - 1.0f, max.y - 1.0f};
            drawList->AddRect(min, borderMax, borderColor, kCornerRounding, 0, kBorderThickness);
        }
    }
} // namespace ThumbnailUi
