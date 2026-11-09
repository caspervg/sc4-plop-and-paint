#pragma once

#include <cfloat>
#include <cstddef>
#include <string>

#include "imgui.h"

// What the panel tabs have in common, so that they look and behave alike.
namespace PanelUi {
    // Between groups of controls on one line.
    inline constexpr float kGroupGap = 16.0f;

    inline constexpr const char* kSearchHelp =
        "Type words in any order. Put a minus before a word to leave it out, for example -winter.";

    inline constexpr const char* kStopPaintingHelp =
        "Stop painting. What you placed stays in the city.";

    // Also on a disabled control, where it says why.
    inline void Tooltip(const char* text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", text);
        }
    }

    // Text that is less important but must be read. The theme's "disabled" color is meant for what
    // is out of use, and is too faint for this, above all over the city.
    [[nodiscard]] inline ImVec4 SecondaryTextColor() {
        constexpr float kTextShare = 0.8f;
        const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const auto blend = [](const float a, const float b) { return a * kTextShare + b * (1.0f - kTextShare); };
        return ImVec4(blend(text.x, background.x), blend(text.y, background.y), blend(text.z, background.z), text.w);
    }

    template <typename... Args>
    void SecondaryText(const char* format, Args... args) {
        ImGui::TextColored(SecondaryTextColor(), format, args...);
    }

    // Quiet text that wraps at the side of the panel.
    inline void Hint(const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, SecondaryTextColor());
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

    // Before BeginPopupModal: a tall dialog scrolls instead of leaving the screen.
    inline void LimitDialogHeight() {
        ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.9f));
    }

    // Offers a corrected search, under NoResults. True when it is chosen.
    [[nodiscard]] inline bool DidYouMean(const std::string& suggestion) {
        ImGui::AlignTextToFramePadding();
        SecondaryText("Did you mean");
        ImGui::SameLine();
        ImGui::PushID("didYouMean");
        const bool chosen = ImGui::SmallButton(suggestion.c_str());
        ImGui::PopID();
        return chosen;
    }

    // Says that nothing matches and offers the way back. True when the filters should be cleared.
    [[nodiscard]] inline bool NoResults(const char* text) {
        Hint(text);
        return ImGui::SmallButton("Clear filters##noResults");  // the result line has one too
    }
}
