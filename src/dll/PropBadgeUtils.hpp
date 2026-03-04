#pragma once

#include <cstdio>
#include <string>

#include "imgui.h"
#include "shared/entities.hpp"

namespace PropBadges {
    constexpr ImU32 kFamilyColor = IM_COL32(48, 102, 96, 255);
    constexpr ImU32 kFamilyHoverColor = IM_COL32(60, 124, 118, 255);
    constexpr ImU32 kDayNightColor = IM_COL32(66, 88, 140, 255);
    constexpr ImU32 kDayNightHoverColor = IM_COL32(80, 104, 162, 255);
    constexpr ImU32 kTimedColor = IM_COL32(138, 98, 36, 255);
    constexpr ImU32 kTimedHoverColor = IM_COL32(160, 114, 46, 255);
    constexpr ImU32 kSeasonalColor = IM_COL32(70, 120, 62, 255);
    constexpr ImU32 kSeasonalHoverColor = IM_COL32(84, 142, 74, 255);
    constexpr ImU32 kChanceColor = IM_COL32(128, 72, 44, 255);
    constexpr ImU32 kChanceHoverColor = IM_COL32(150, 86, 54, 255);

    inline bool HasSeasonalTiming(const Prop& prop) {
        return prop.simulatorDateStart.has_value() ||
            prop.simulatorDateDuration.has_value() ||
            prop.simulatorDateInterval.has_value();
    }

    inline std::string BuildBehaviorSummary(const Prop& prop) {
        const bool hasDayNight = prop.nighttimeStateChange.value_or(false);
        const bool hasTimeWindow = prop.timeOfDay.has_value();
        const bool hasSeasonalTiming = HasSeasonalTiming(prop);

        if (hasDayNight && hasTimeWindow && hasSeasonalTiming) {
            return "Day/Night + Timed + Seasonal";
        }
        if (hasDayNight && hasTimeWindow) {
            return "Day/Night + Timed";
        }
        if (hasDayNight && hasSeasonalTiming) {
            return "Day/Night + Seasonal";
        }
        if (hasTimeWindow && hasSeasonalTiming) {
            return "Timed + Seasonal";
        }
        if (hasDayNight) {
            return "Day/Night";
        }
        if (hasTimeWindow) {
            return "Timed";
        }
        if (hasSeasonalTiming) {
            return "Seasonal";
        }

        return "Static";
    }

    inline void RenderPill(const char* label, const ImU32 baseColor, const ImU32 hoverColor) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, baseColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, hoverColor);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(235, 238, 242, 255));
        ImGui::SmallButton(label);
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    }

    template <typename F>
    inline void ForEachBadge(const Prop& prop, F&& fn) {
        if (!prop.familyIds.empty()) {
            fn("Family", kFamilyColor, kFamilyHoverColor);
        }
        if (prop.nighttimeStateChange.value_or(false)) {
            fn("Day/Night", kDayNightColor, kDayNightHoverColor);
        }
        if (prop.timeOfDay.has_value()) {
            fn("Timed", kTimedColor, kTimedHoverColor);
        }
        if (HasSeasonalTiming(prop)) {
            fn("Seasonal", kSeasonalColor, kSeasonalHoverColor);
        }
        if (prop.randomChance.has_value() && *prop.randomChance < 100) {
            char buffer[24]{};
            std::snprintf(buffer, sizeof(buffer), "%u%% Chance", *prop.randomChance);
            fn(buffer, kChanceColor, kChanceHoverColor);
        }
    }
}
