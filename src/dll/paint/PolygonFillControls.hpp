#pragma once

#include "PaintSettings.hpp"
#include "imgui.h"

// The polygon mode controls of the paint popups.
inline void RenderPolygonFillControls(PropPaintSettings& settings) {
    int fill = static_cast<int>(settings.polygonFill);
    ImGui::Combo("Fill", &fill, "Scatter\0Rows\0");
    settings.polygonFill = static_cast<PolygonFillStyle>(fill);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Scatter puts items at random places. Rows puts items in straight rows, like a crop field.");
    }

    if (settings.polygonFill == PolygonFillStyle::Rows) {
        ImGui::SliderFloat("Row spacing (m)", &settings.rowSpacingMeters, 0.5f, 50.0f, "%.1f");
        ImGui::SliderFloat("Step along row (m)", &settings.rowStepMeters, 0.25f, 50.0f, "%.2f");
        ImGui::SliderFloat("Row angle (deg)", &settings.rowAngleDegrees, 0.0f, 180.0f, "%.0f");
        ImGui::Checkbox("Turn items with the rows", &settings.alignToPath);
    }
    else {
        ImGui::SliderFloat("Density (/100 m^2)", &settings.densityPer100Sqm, 0.1f, 10.0f, "%.2f");
        ImGui::SliderFloat("Density variation", &settings.densityVariation, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 gives an even fill. 1 gives more clusters and gaps.");
        }
    }

    ImGui::SliderFloat("Edge margin (m)", &settings.edgeMarginMeters, 0.0f, 50.0f, "%.1f");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Keep items this far inside the edge of the polygon.");
    }
    ImGui::Checkbox("Random rotation", &settings.randomRotation);
}
