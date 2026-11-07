#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "imgui.h"

// Thumbnail tiles in rows that fill the available width.
//
// Every tile is exactly tileSize square whether or not its texture has loaded, so all rows have the
// same height and the list clipper's row height is exact. (The decal grid used to mix a padded
// ImageButton for loaded tiles with an unpadded Button for pending ones, so rows changed height as
// thumbnails arrived and the grid jumped around while scrolling.)
namespace ThumbnailGrid {
    // Calls drawTile(index) for every visible tile, right after an invisible button covering the
    // tile, so IsItemHovered, IsItemClicked, BeginPopupContextItem and GetItemRectMin inside
    // drawTile refer to that tile. idOf(index) gives the item's stable id, so a tile's popup stays
    // with its item when the list changes. Returns how many tiles fit on screen, for sizing the
    // texture cache.
    template <typename IdOf, typename DrawTile>
    size_t Render(const size_t count, const float tileSize, IdOf&& idOf, DrawTile&& drawTile) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int columns = std::max(1, static_cast<int>((avail.x + style.ItemSpacing.x) /
                                                         (tileSize + style.ItemSpacing.x)));
        const float rowHeight = tileSize + style.ItemSpacing.y;
        const int rows = static_cast<int>((count + columns - 1) / columns);

        ImGuiListClipper clipper;
        clipper.Begin(rows, rowHeight);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int col = 0; col < columns; ++col) {
                    const size_t index = static_cast<size_t>(row) * columns + col;
                    if (index >= count) {
                        break;
                    }
                    if (col > 0) {
                        ImGui::SameLine(0.0f, style.ItemSpacing.x);
                    }
                    const auto id = static_cast<uint64_t>(idOf(index));
                    ImGui::PushID(static_cast<int>(id ^ (id >> 32)));
                    ImGui::InvisibleButton("##tile", ImVec2(tileSize, tileSize),
                                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                    drawTile(index);
                    ImGui::PopID();
                }
            }
        }

        // One extra row above and below covers partly visible rows.
        const int visibleRows = static_cast<int>(ImGui::GetWindowHeight() / rowHeight) + 2;
        return static_cast<size_t>(visibleRows) * columns;
    }

    // Scroll offset that puts the row holding tile `index` at the top: ImGui::SetScrollY(ScrollYFor(...)).
    [[nodiscard]] inline float ScrollYFor(const size_t index, const float tileSize) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) /
                                                         (tileSize + style.ItemSpacing.x)));
        return static_cast<float>(index / static_cast<size_t>(columns)) * (tileSize + style.ItemSpacing.y);
    }

    // Writes a name inside the tile drawn last, for items without a thumbnail.
    inline void LabelLastTile(const char* text) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec4 clip(min.x + 3.0f, min.y + 3.0f, max.x - 3.0f, max.y - 3.0f);
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.85f,
                                            ImVec2(clip.x, clip.y), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                            text, nullptr, clip.z - clip.x, &clip);
    }

    // Selection outline, hover highlight and a favourite star on the tile drawn last.
    inline void DecorateLastTile(const bool selected, const bool favorite, const bool favoriteHighlighted = false) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();

        if (ImGui::IsItemHovered()) {
            drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_ButtonHovered, 0.35f), 4.0f);
        }
        if (selected) {
            drawList->AddRect(ImVec2(min.x - 2.0f, min.y - 2.0f), ImVec2(max.x + 2.0f, max.y + 2.0f),
                              IM_COL32(255, 200, 0, 255), 2.0f, 0, 2.0f);
        }
        if (favorite) {
            const ImVec2 badgeMin(min.x + 3.0f, min.y + 3.0f);
            const ImVec2 badgeMax(min.x + 18.0f, min.y + 17.0f);
            drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(32, 32, 32, 220), 3.0f);
            drawList->AddText(ImVec2(badgeMin.x + 4.0f, badgeMin.y + 1.0f),
                              favoriteHighlighted ? IM_COL32(255, 210, 64, 255) : IM_COL32(220, 220, 220, 255),
                              "*");
        }
    }
} // namespace ThumbnailGrid
