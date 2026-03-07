#pragma once
#include "imgui.h"

namespace UI {
    // Shared
    inline float wideInputWidth() { return 7.7f * ImGui::GetFontSize(); }
    inline float dropdownWidth() { return 7.7f * ImGui::GetFontSize(); }
    constexpr auto kIconSize = 44.0f; // fixed: pre-rendered texture size
    inline float iconColumnWidth() { return kIconSize + 0.1f * ImGui::GetFontSize(); }
    inline float actionColumnWidth() { return 8.5f * ImGui::GetFontSize(); }
    constexpr auto kMeterFloatFormat = "%.1f m";

    // Props tab
    inline float propSizeColumnWidth() { return 6.0f * ImGui::GetFontSize(); }

    // Row heights
    inline float iconRowHeight() { return kIconSize + 0.6f * ImGui::GetFontSize(); }

    // Buildings tab
    inline float lotsCountColumnWidth() { return 3.1f * ImGui::GetFontSize(); }
    inline float lotSizeColumnWidth() { return 3.1f * ImGui::GetFontSize(); }
    inline float lotStageColumnWidth() { return 2.7f * ImGui::GetFontSize(); }

    // Families tab
    inline float iidFilterWidth() { return 13.8f * ImGui::GetFontSize(); }
    inline float familyTableHeight() { return 13.8f * ImGui::GetFontSize(); }
    inline float typeColumnWidth() { return 7.0f * ImGui::GetFontSize(); }
    inline float instanceIdColumnWidth() { return 8.5f * ImGui::GetFontSize(); }
    inline float propsColumnWidth() { return 5.4f * ImGui::GetFontSize(); }
    inline float familyActionColWidth() { return 6.9f * ImGui::GetFontSize(); }
    inline float familyEntriesHeight() { return 19.2f * ImGui::GetFontSize(); }
    inline float weightColumnWidth() { return 9.2f * ImGui::GetFontSize(); }
    inline float removeColumnWidth() { return 1.85f * ImGui::GetFontSize(); }

    // Flora tab
    inline float floraTableHeight() { return 13.8f * ImGui::GetFontSize(); }
    inline float floraGroupsTableHeight() { return 10.5f * ImGui::GetFontSize(); }
    inline float floraStagesHeight() { return 10.8f * ImGui::GetFontSize(); }
    inline float favoriteColumnWidth() { return 4.6f * ImGui::GetFontSize(); }
    inline float floraStageColumnWidth() { return 4.0f * ImGui::GetFontSize(); }

    // Occupant groups
    inline float treeIndentSpacing() { return 0.9f * ImGui::GetFontSize(); }
    inline float ogTreeHeight() { return 11.5f * ImGui::GetFontSize(); }
} // namespace UI

namespace Cache {
    constexpr auto kMaxSize = 250uz;
    constexpr auto kMaxLoadPerFrame = 25;
    constexpr auto kPrefetchMargin = 10;
} // namespace Cache

namespace Undo {
    // Maximum number of undo groups retained in the pending placements stack.
    // Older groups are automatically committed (de-highlighted) when the limit is exceeded.
    constexpr size_t kMaxUndoGroups = 50;
} // namespace Undo

constexpr auto kMaxIconsToLoadPerFrame = 50;
