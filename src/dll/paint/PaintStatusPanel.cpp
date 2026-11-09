#include "PaintStatusPanel.hpp"

#include "../decals/DecalPainterInputControl.hpp"
#include "../common/PanelUi.hpp"
#include "../decals/FractionalAngles.hpp"
#include "../props/PropPainterInputControl.hpp"
#include "imgui.h"

#include <cstdio>

namespace {
    void StateRow(const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        PanelUi::SecondaryText("%s", label);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(value);
    }

    void KeyRow(const char* keys, const char* action) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(keys);
        ImGui::TableNextColumn();
        PanelUi::SecondaryText("%s", action);
    }
}

void PaintStatusPanel::OnRender() {
    if (!visible_ || !activeControl_) {
        return;
    }

    const auto& settings = activeControl_->GetSettings();

    ImGui::SetNextWindowBgAlpha(0.85f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoDocking;

    if (!ImGui::Begin("##PropPaintStatus", nullptr, kFlags)) {
        ImGui::End();
        return;
    }

    const auto* decal = dynamic_cast<const DecalPainterInputControl*>(activeControl_);
    const bool vertical = activeControl_->SupportsVerticalAdjustment();
    const bool direct = settings.mode == PaintMode::Direct;
    const bool line = settings.mode == PaintMode::Line;
    const bool polygon = settings.mode == PaintMode::Polygon;
    const bool rows = polygon && settings.polygonFill == PolygonFillStyle::Rows;
    const bool sketch = activeControl_->HasPendingSketch() || activeControl_->HasGroundSketch();

    constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_SizingFixedFit;
    char value[96];
    const auto state = [&](const char* label, const char* format, const auto... args) {
        std::snprintf(value, sizeof(value), format, args...);
        StateRow(label, value);
    };

    if (ImGui::BeginTable("##state", 2, kTableFlags)) {
        if (const std::string& name = activeControl_->GetPaintName(); !name.empty()) {
            state("Painting", "%.48s", name.c_str());
        }
        static constexpr const char* kModeNames[] = {"Direct", "Line", "Polygon"};
        state("Mode", "%s%s", kModeNames[static_cast<int>(settings.mode)], settings.pickFromGround ? " on painted ground" : "");

        if (decal) {
            state("Rotation", "%s", fa_angles::Label(decal->GetRotationDegrees()).c_str());
            if (decal->GetDepthOffset() >= 0) {
                state("Depth offset", "%d", decal->GetDepthOffset());
            }
            else {
                state("Depth offset", "%s", "Default");
            }
            if (decal->IsMirrored()) {
                state("Mirror", "%s", "On");
            }
        }
        else if (line && settings.alignToPath) {
            state("Rotation", "%s", "Along the line");
        }
        else if (settings.randomRotation) {
            state("Rotation", "%s", "Random");
        }
        else {
            state("Rotation", "%d deg", (settings.rotation & 3) * 90);
        }

        state("Grid", "%s, %.1f m", settings.showGrid ? "On" : "Off", settings.gridStepMeters);
        state("Snap", "%s", !settings.snapPointsToGrid ? "Off" : settings.snapPlacementsToGrid ? "On, also placements" : "On");
        state("Preview", "%s", kPreviewModeLabels[static_cast<int>(settings.previewMode)]);

        if (vertical) {
            state("Height offset", "%+.1f m", settings.deltaYMeters);
        }
        if (direct && vertical && activeControl_->HasCapturedDirectAbsoluteHeight()) {
            state("Captured height", "%.1f m", activeControl_->GetCapturedDirectAbsoluteHeight());
        }
        if (line) {
            state("Spacing", "%.1f m", settings.spacingMeters);
        }
        else if (rows) {
            state("Rows", "%.1f m apart, every %.2f m", settings.rowSpacingMeters, settings.rowStepMeters);
        }
        else if (polygon) {
            state("Density", "%.2f per 100 m2", settings.densityPer100Sqm);
            state("Variation", "%.2f", settings.densityVariation);
        }
        if (vertical && !direct && settings.sketchHeightMode == SketchHeightMode::Custom) {
            cS3DVector3 unusedTerrainPos;
            cS3DVector3 resolvedPos;
            if (activeControl_->TryGetCursorSketchPreview(unusedTerrainPos, resolvedPos)) {
                state("Node height", "%+.1f m, at %.1f m", activeControl_->GetSketchCaptureOffsetMeters(), resolvedPos.fY);
            }
            else {
                state("Node height", "%+.1f m", activeControl_->GetSketchCaptureOffsetMeters());
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    if (ImGui::BeginTable("##keys", 2, kTableFlags)) {
        // What ends the work comes first.
        if (activeControl_->IsPickingGround()) {
            KeyRow("Click", "Choose painted ground");
        }
        if (activeControl_->HasGroundSketch()) {
            KeyRow("Click", "Start over");
        }
        KeyRow("Enter", !sketch ? "Confirm what you placed" : line ? "Place along the line" : "Fill the shape");
        KeyRow("Esc", activeControl_->HasPendingSketch() ? "Clear the sketch" : "Stop and keep what you placed");
        if (!direct) {
            KeyRow("Backspace", "Remove the last point");
        }
        KeyRow(decal ? "Ctrl+Z" : "Ctrl+Z / back mouse", "Undo");
        if (!direct && !decal) {
            KeyRow("Ctrl+Backspace", "Undo one item of the group");
        }

        if (decal) {
            KeyRow("R / Home", "Turn 45 degrees, Shift 5 degrees");
            KeyRow("End", "Turn the other way");
            KeyRow("F / Shift+F", "Next / previous FA angle");
            KeyRow("M", "Mirror");
            KeyRow("Ctrl+PgUp / PgDn", "Depth offset");
        }
        else {
            KeyRow("R / Home / middle mouse", "Turn 90 degrees");
            KeyRow("End", "Turn the other way");
            if (const auto stop = activeControl_->NextStop()) {
                std::snprintf(value, sizeof(value), "Next mode: %s", PaintStopName(*stop));
                KeyRow("M", value);
            }
        }
        if (activeControl_->CanSkipEntry()) {
            KeyRow("Tab / Shift+Tab", "Next / previous item");
        }

        KeyRow("G / Shift+S", "Grid / snap on or off");
        KeyRow("Ctrl+[ ]", "Grid size");
        KeyRow("P", "Preview");

        if (vertical) {
            KeyRow("[ ]", "Height 1.0 m, Shift 5.0 m, Shift+Alt 0.1 m");
            KeyRow("0", "Height back to 0");
        }
        if (direct && vertical) {
            KeyRow("H", "Capture the height");
        }
        if (!direct) {
            KeyRow("- +", line ? "Spacing" : rows ? "Space between rows" : "Density");
        }
        if (polygon) {
            KeyRow("Ctrl+- +", rows ? "Step along the row" : "Variation");
        }
        if (!direct && activeControl_->CanUseGroundShapes()) {
            KeyRow("F", polygon ? "Fill the painted ground under the cursor" : "Plant along the painted ground under the cursor");
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

void PaintStatusPanel::SetActiveControl(BasePainterInputControl* control) {
    activeControl_ = control;
}

void PaintStatusPanel::SetVisible(const bool visible) {
    visible_ = visible;
    if (!visible) {
        activeControl_ = nullptr;
    }
}
