#pragma once

#include <cstdio>
#include <string>
#include <utility>

#include "imgui.h"
#include "shared/entities.hpp"

namespace Badges {
    constexpr ImU32 kFamilyColor = IM_COL32(48, 102, 96, 255);
    constexpr ImU32 kFamilyHoverColor = IM_COL32(60, 124, 118, 255);
    constexpr ImU32 kDayNightColor = IM_COL32(66, 88, 140, 255);
    constexpr ImU32 kDayNightHoverColor = IM_COL32(80, 104, 162, 255);
    constexpr ImU32 kTimedColor = IM_COL32(138, 98, 36, 255);
    constexpr ImU32 kTimedHoverColor = IM_COL32(160, 114, 46, 255);
    constexpr ImU32 kSeasonalColor = IM_COL32(70, 120, 62, 255);
    constexpr ImU32 kSeasonalHoverColor = IM_COL32(84, 142, 74, 255);
    constexpr ImU32 kSeasonalLooseColor = IM_COL32(104, 108, 52, 255);
    constexpr ImU32 kSeasonalLooseHoverColor = IM_COL32(122, 126, 62, 255);
    constexpr ImU32 kChanceColor = IM_COL32(128, 72, 44, 255);
    constexpr ImU32 kChanceHoverColor = IM_COL32(150, 86, 54, 255);
    constexpr ImU32 kWallToWallColor = IM_COL32(98, 72, 132, 255);
    constexpr ImU32 kWallToWallHoverColor = IM_COL32(118, 88, 154, 255);

    inline bool HasSeasonalTiming(const Prop& prop) {
        return prop.simulatorDateStart.has_value() ||
            prop.simulatorDateDuration.has_value() ||
            prop.simulatorDateInterval.has_value();
    }

    inline std::string BuildBehaviorSummary(const Prop& prop, const SeasonalSet* seasonalSet = nullptr) {
        std::string summary;
        const auto append = [&summary](const char* part) {
            if (!summary.empty()) {
                summary += " + ";
            }
            summary += part;
        };

        if (prop.nighttimeStateChange.value_or(false)) {
            append("Day/Night");
        }
        if (prop.timeOfDay.has_value()) {
            append("Timed");
        }
        if (seasonalSet != nullptr) {
            append("Seasonal set");
        }
        else if (HasSeasonalTiming(prop)) {
            append("Seasonal (no set)");
        }

        return summary.empty() ? "Static" : summary;
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
    inline void ForEachBadge(const Prop& prop, const SeasonalSet* seasonalSet, F&& fn) {
        if (!prop.familyIds.empty()) {
            fn("Family", kFamilyColor, kFamilyHoverColor);
        }
        if (prop.nighttimeStateChange.value_or(false)) {
            fn("Day/Night", kDayNightColor, kDayNightHoverColor);
        }
        if (prop.timeOfDay.has_value()) {
            fn("Timed", kTimedColor, kTimedHoverColor);
        }
        if (seasonalSet != nullptr) {
            char buffer[24]{};
            std::snprintf(buffer, sizeof(buffer), "Seasonal set (%zu)", seasonalSet->members.size());
            fn(buffer, kSeasonalColor, kSeasonalHoverColor);
        }
        else if (HasSeasonalTiming(prop)) {
            fn("Seasonal", kSeasonalLooseColor, kSeasonalLooseHoverColor);
        }
        if (prop.randomChance.has_value() && *prop.randomChance < 100) {
            char buffer[24]{};
            std::snprintf(buffer, sizeof(buffer), "%u%% Chance", *prop.randomChance);
            fn(buffer, kChanceColor, kChanceHoverColor);
        }
    }

    template <typename F>
    inline void ForEachBadge(const Prop& prop, F&& fn) {
        ForEachBadge(prop, nullptr, std::forward<F>(fn));
    }
}
