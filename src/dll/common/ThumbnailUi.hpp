#pragma once

#include <optional>

#include "imgui.h"

namespace ThumbnailUi {
    constexpr float kCornerRounding = 4.0f;
    constexpr float kBorderThickness = 1.0f;

    // Draws a thumbnail slot (background, image, border) into the rectangle min..min+size.
    inline void Draw(ImDrawList* drawList,
                     const ImVec2 min,
                     const ImVec2 size,
                     const std::optional<void*> textureId,
                     const ImU32 backgroundColor,
                     const ImU32 borderColor) {
        const ImVec2 max{min.x + size.x, min.y + size.y};

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

    // Draws a thumbnail slot at the cursor and advances it.
    inline void Render(const std::optional<void*> textureId,
                       const ImVec2 size,
                       const ImU32 backgroundColor,
                       const ImU32 borderColor) {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        Draw(ImGui::GetWindowDrawList(), min, size, textureId, backgroundColor, borderColor);
    }
} // namespace ThumbnailUi
