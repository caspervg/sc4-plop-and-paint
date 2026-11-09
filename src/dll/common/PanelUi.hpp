#pragma once

#include <cstddef>

#include "imgui.h"

// What the panel tabs have in common, so that they look and behave alike.
namespace PanelUi {
    // Between groups of controls on one line.
    inline constexpr float kGroupGap = 16.0f;

    inline constexpr const char* kStopPaintingHelp =
        "Stop painting. What you did not confirm with Enter is removed.";

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
        else {
            ImGui::AlignTextToFramePadding();  // a small button lines up with taller controls after it
        }
    }

    // The width of a checkbox with this label.
    [[nodiscard]] inline float CheckboxWidth(const char* label) {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
    }

    // The width of a button with this label.
    [[nodiscard]] inline float ButtonWidth(const char* label) {
        return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    // A popup, opened with ImGui::OpenPopup(popupId), that asks before something is deleted for
    // good. True in the frame it is confirmed.
    [[nodiscard]] inline bool ConfirmDeletePopup(const char* popupId, const char* name) {
        bool confirmed = false;
        if (ImGui::BeginPopup(popupId)) {
            ImGui::Text("Delete \"%s\"? You cannot undo this.", name);
            if (ImGui::SmallButton("Delete")) {
                confirmed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.0f, kGroupGap);
            if (ImGui::SmallButton("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return confirmed;
    }

    // Says that nothing matches and offers the way back. True when the filters should be cleared.
    [[nodiscard]] inline bool NoResults(const char* text) {
        Hint(text);
        return ImGui::SmallButton("Clear filters##noResults");  // the result line has one too
    }
}
