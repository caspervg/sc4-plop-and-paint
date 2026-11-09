#pragma once

#include <cstddef>

#include "imgui.h"

// What the panel tabs have in common, so that they look and behave alike.
namespace PanelUi {
    // Between groups of controls on one line.
    inline constexpr float kGroupGap = 16.0f;

    // Also on a disabled control, where it says why.
    inline void Tooltip(const char* text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", text);
        }
    }

    // Quiet text that wraps at the side of the panel.
    inline void Hint(const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    // Keeps a row of controls inside the panel: the next one goes on this line if it fits.
    // index is its place in the row, width its width.
    inline void NextInRow(const size_t index, const float width, const float gap = -1.0f) {
        if (index == 0) {
            return;
        }
        const float space = gap >= 0.0f ? gap : ImGui::GetStyle().ItemSpacing.x;
        const float right = ImGui::GetItemRectMax().x + space + width;
        if (right < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) {
            ImGui::SameLine(0.0f, space);
        }
    }

    // The width of a checkbox with this label.
    [[nodiscard]] inline float CheckboxWidth(const char* label) {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
    }

    // Says that nothing matches and offers the way back. True when the filters should be cleared.
    [[nodiscard]] inline bool NoResults(const char* text) {
        Hint(text);
        return ImGui::SmallButton("Clear filters");
    }
}
