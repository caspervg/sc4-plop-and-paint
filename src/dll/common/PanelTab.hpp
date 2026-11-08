#pragma once
#include <cstdint>
#include <optional>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../favorites/FavoritesRepository.hpp"
#include "../lots/LotRepository.hpp"
#include "../props/PropRepository.hpp"
#include "Constants.hpp"
#include "ThumbnailGrid.hpp"
#include "ThumbnailUi.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "imgui.h"
#include "imgui_internal.h"


class cIGZImGuiService;

class PanelTab {
public:
    PanelTab(SC4PlopAndPaintDirector* director,
             LotRepository* lots,
             PropRepository* props,
             FavoritesRepository* favorites,
             cIGZImGuiService* imguiService)
        : director_(director)
        , lots_(lots)
        , props_(props)
        , favorites_(favorites)
        , imguiService_(imguiService) {}

    virtual ~PanelTab() = default;

    [[nodiscard]] virtual const char* GetTabName() const = 0;

    virtual void OnRender() = 0;

    virtual void OnDeviceReset(uint32_t deviceGeneration) = 0;

    // Called before the ImGui service is released during shutdown.
    // Subclasses should release any textures/resources that depend on the service.
    virtual void OnShutdown() {}

    // Abandons all textures without calling the service.
    // Use during DLL teardown when the service may already be destroyed.
    virtual void Abandon() {}

protected:
    static void ReleaseImGuiInputCapture_() {
        if (ImGui::GetCurrentContext()) {
            ImGui::ClearActiveID();
            ImGui::SetWindowFocus(nullptr);
        }
    }

    void RenderPropStripperControls_() const {
        if (!director_) {
            return;
        }

        ImGui::SameLine();
        uint32_t sourceFlags = director_->GetPropStripperSources();
        bool cityEnabled = (sourceFlags & PropStripperInputControl::SourceFlagCity) != 0;
        bool lotEnabled = (sourceFlags & PropStripperInputControl::SourceFlagLot) != 0;
        bool streetEnabled = (sourceFlags & PropStripperInputControl::SourceFlagStreet) != 0;
        bool sourceChanged = false;

        sourceChanged = ImGui::Checkbox("City##PropStripCity", &cityEnabled) || sourceChanged;
        ImGui::SameLine(0.0f, 6.0f);
        sourceChanged = ImGui::Checkbox("Lot##PropStripLot", &lotEnabled) || sourceChanged;
        ImGui::SameLine(0.0f, 6.0f);
        sourceChanged = ImGui::Checkbox("Street##PropStripStreet", &streetEnabled) || sourceChanged;

        if (sourceChanged) {
            uint32_t newFlags = PropStripperInputControl::SourceFlagNone;
            if (cityEnabled) {
                newFlags |= PropStripperInputControl::SourceFlagCity;
            }
            if (lotEnabled) {
                newFlags |= PropStripperInputControl::SourceFlagLot;
            }
            if (streetEnabled) {
                newFlags |= PropStripperInputControl::SourceFlagStreet;
            }
            director_->SetPropStripperSources(newFlags);
        }

        if (director_->IsPropStripping()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop stripping")) {
                director_->StopPropStripping();
            }
        }
        else {
            ImGui::SameLine();
            const bool hasAnySource = cityEnabled || lotEnabled || streetEnabled;
            if (!hasAnySource) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Strip props")) {
                ReleaseImGuiInputCapture_();
                director_->StartPropStripping();
            }
            if (!hasAnySource) {
                ImGui::EndDisabled();
            }
            if (ImGui::IsItemHovered()) {
                if (hasAnySource) {
                    ImGui::SetTooltip("Click a prop to remove it.\nPress B to turn brush mode on or off.\nIn brush mode, hold the left mouse button to remove all props in the circle.\nPress Ctrl+Z to put back city, lot and street props.\nPress Esc to stop.");
                }
                else {
                    ImGui::SetTooltip("Turn on at least one prop source before you start.");
                }
            }
        }
    }

    void RenderFloraStripperControls_() const {
        if (!director_) {
            return;
        }

        if (director_->IsFloraStripping()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Stop stripping")) {
                director_->StopFloraStripping();
            }
        }
        else {
            ImGui::SameLine();
            if (ImGui::SmallButton("Strip flora")) {
                ReleaseImGuiInputCapture_();
                director_->StartFloraStripping();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click flora in the city to remove it.\nPress B to turn brush mode on or off.\nIn brush mode, hold the left mouse button to remove all flora in the circle.\nPress Ctrl+Z to undo.\nPress Esc to stop.");
            }
        }
    }

    void RenderThumbnail_(const std::optional<void*> textureId,
                          const ImVec2 size = ImVec2(UI::kIconSize, UI::kIconSize)) const {
        ThumbnailUi::Render(
            textureId,
            size,
            director_ ? director_->GetThumbnailBackgroundColor() : IM_COL32(0, 0, 0, 0),
            director_ ? director_->GetThumbnailBorderColor() : IM_COL32(0, 0, 0, 0));
    }

    // A list-row thumbnail. Hovering it shows the larger stored thumbnail in a tooltip. The row's
    // selectable spans the thumbnail, so this tests the rectangle rather than the item.
    void RenderThumbnailWithPreview_(const std::optional<void*> textureId, ThumbnailStore& store, const uint64_t key) {
        RenderThumbnail_(textureId);
        if (HasLargePreview_(store, key) && ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())) {
            ImGui::BeginTooltip();
            RenderLargePreview_(store, key);
            ImGui::EndTooltip();
        }
    }

    // Draws a thumbnail into the tile ThumbnailGrid::Render just laid out.
    void RenderGridTile_(const std::optional<void*> textureId) const {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ThumbnailUi::Draw(
            ImGui::GetWindowDrawList(),
            min,
            ImVec2(max.x - min.x, max.y - min.y),
            textureId,
            director_ ? director_->GetThumbnailBackgroundColor() : IM_COL32(0, 0, 0, 0),
            director_ ? director_->GetThumbnailBorderColor() : IM_COL32(0, 0, 0, 0));
    }

    // "Grid" toggle and tile size, on the current line. Returns true when either changed, which
    // means cached thumbnails have the wrong size and the caller should clear its cache.
    bool RenderViewControls_() {
        static constexpr float kTileSizes[] = {44.0f, 66.0f, 88.0f};
        static constexpr const char* kTileLabels[] = {"Small", "Medium", "Large"};

        if (!viewLoaded_) {
            viewLoaded_ = true;
            if (const TabViewSettings* saved = favorites_ ? favorites_->GetTabView(GetTabName()) : nullptr) {
                gridView_ = saved->grid;
                for (int i = 0; i < 3; ++i) {
                    if (static_cast<uint32_t>(kTileSizes[i]) == saved->tileSize) {
                        gridTileSizeIndex_ = i;
                    }
                }
            }
        }

        bool changed = false;
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Grid", &gridView_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show only the thumbnails.\nMove the mouse over a thumbnail to see its details.\nDouble-click a thumbnail to paint with it.\nRight-click a thumbnail for more options.");
        }
        if (gridView_) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(6.0f * ImGui::GetFontSize());
            if (ImGui::BeginCombo("##TileSize", kTileLabels[gridTileSizeIndex_])) {
                for (int i = 0; i < 3; ++i) {
                    if (ImGui::Selectable(kTileLabels[i], i == gridTileSizeIndex_) && i != gridTileSizeIndex_) {
                        gridTileSizeIndex_ = i;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        gridTileSize_ = kTileSizes[gridTileSizeIndex_];
        if (changed && favorites_) {
            favorites_->SetTabView({GetTabName(), gridView_, static_cast<uint32_t>(gridTileSize_)});
        }
        return changed;
    }

    // Size that thumbnails are shown at in the current view.
    [[nodiscard]] float ShownThumbnailSize_() const {
        return gridView_ ? gridTileSize_ : UI::kIconSize;
    }

    // Whether the cache stores this thumbnail noticeably larger than it is shown now.
    [[nodiscard]] bool HasLargePreview_(const ThumbnailStore& store, const uint64_t key) const {
        return imguiService_ && static_cast<float>(store.StoredSize()) >= ShownThumbnailSize_() * 1.25f &&
               store.HasThumbnail(key);
    }

    // A larger thumbnail for a tooltip, at the size stored in the cache (up to the cache builder's
    // maximum). Draws nothing when HasLargePreview_ is false.
    void RenderLargePreview_(ThumbnailStore& store, const uint64_t key) {
        if (!HasLargePreview_(store, key)) {
            return;
        }
        const auto storedSize = static_cast<float>(store.StoredSize());
        previewCache_.Request(key);
        previewCache_.ProcessLoadQueue([&](const uint64_t previewKey) {
            ImGuiTexture texture;
            if (const auto data = store.LoadThumbnail(previewKey)) {
                texture.Create(imguiService_, data->width, data->height, data->rgba.data());
            }
            return texture;
        });
        RenderThumbnail_(previewCache_.Get(key), ImVec2(storedSize, storedSize));
    }

    bool viewLoaded_ = false;  // view settings read from favorites on first use
    bool gridView_ = false;
    int gridTileSizeIndex_ = 1;
    float gridTileSize_ = 66.0f;
    ThumbnailCache<uint64_t> previewCache_{4, 1};

    SC4PlopAndPaintDirector* director_;  // game actions only
    LotRepository*       lots_;
    PropRepository*      props_;
    FavoritesRepository* favorites_;
    cIGZImGuiService*    imguiService_;
};
