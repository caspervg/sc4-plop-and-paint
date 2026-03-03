#include "PropPaintStatusPanel.hpp"
#include "imgui.h"

void PropPaintStatusPanel::OnRender() {
    if (!visible_ || !activeControl_) {
        return;
    }

    const auto& settings = activeControl_->GetSettings();

    ImGui::SetNextWindowBgAlpha(0.7f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs;

    if (!ImGui::Begin("##PropPaintStatus", nullptr, kFlags)) {
        ImGui::End();
        return;
    }

    // Mode
    static constexpr const char* kModeNames[] = {"Direct", "Line", "Polygon"};
    const int modeIdx = static_cast<int>(settings.mode);
    ImGui::Text("Mode: %s", kModeNames[modeIdx]);

    // Grid
    ImGui::Text("Grid: %s (%.1fm)", settings.showGrid ? "ON" : "OFF", settings.gridStepMeters);

    // Snap
    if (settings.snapPointsToGrid) {
        ImGui::Text("Snap: ON%s", settings.snapPlacementsToGrid ? " + placements" : "");
    } else {
        ImGui::TextUnformatted("Snap: OFF");
    }

    // Preview mode
    static constexpr const char* kPreviewNames[] = {"Outline", "Full", "Combined", "Hidden"};
    const int previewIdx = static_cast<int>(settings.previewMode);
    ImGui::Text("Preview: %s", kPreviewNames[previewIdx]);

    // Mode-specific settings
    if (settings.mode == PropPaintMode::Line) {
        ImGui::Text("Spacing: %.1fm", settings.spacingMeters);
    } else if (settings.mode == PropPaintMode::Polygon) {
        ImGui::Text("Density: %.1f/100m^2", settings.densityPer100Sqm);
    }

    // Hotkey hints
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::TextUnformatted("R rotation  G grid  S snap");
    ImGui::TextUnformatted("[ ] grid size  P preview");
    ImGui::TextUnformatted("Ctrl+Z undo group  Ctrl+Backspace undo prop");
    if (settings.mode == PropPaintMode::Line || settings.mode == PropPaintMode::Polygon) {
        ImGui::TextUnformatted("-/+ spacing/density");
    }
    ImGui::PopStyleColor();

    ImGui::End();
}

void PropPaintStatusPanel::SetActiveControl(PropPainterInputControl* control) {
    activeControl_ = control;
}

void PropPaintStatusPanel::SetVisible(const bool visible) {
    visible_ = visible;
    if (!visible) {
        activeControl_ = nullptr;
    }
}
