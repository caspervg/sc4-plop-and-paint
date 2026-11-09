#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>

#include "../SC4PlopAndPaintDirector.hpp"
#include "../paint/PolygonFillControls.hpp"
#include "../favorites/FavoritesRepository.hpp"
#include "../lots/LotRepository.hpp"
#include "../props/PropRepository.hpp"
#include "Constants.hpp"
#include "PanelUi.hpp"
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

    // A row of small buttons that each set one value. Returns true when one was pressed.
    static bool RenderPresetButtons_(const char* id, float& value, const std::initializer_list<float> presets) {
        bool pressed = false;
        ImGui::PushID(id);
        for (const float preset : presets) {
            char label[16];
            std::snprintf(label, sizeof(label), "%g", preset);
            if (ImGui::SmallButton(label)) {
                value = preset;
                pressed = true;
            }
            ImGui::SameLine();
        }
        ImGui::PopID();
        ImGui::NewLine();
        return pressed;
    }

    // Dragging snaps to the step. Ctrl+click still takes any value.
    static void RenderSteppedSlider_(const char* label, float& value, const float min, const float max, const float step,
                                     const std::initializer_list<float> presets) {
        // Read before the slider: it closes the text box in the frame that applies the typed value.
        // Not ImGui::TempInputIsActive, which is inline and reads GImGui. That symbol cannot be
        // imported from the delay-loaded SC4RenderServices.dll.
        const ImGuiContext& context = *ImGui::GetCurrentContext();
        const ImGuiID id = ImGui::GetID(label);
        const bool typing = context.ActiveId == id && context.TempInputId == id;
        if (ImGui::SliderFloat(label, &value, min, max, "%.1f") && !typing) {
            value = std::round(value / step) * step;
        }
        ImGui::SetItemTooltip("Ctrl+click to type an exact value.");
        RenderPresetButtons_(label, value, presets);
    }

    static void RenderSpacingControl_(float& spacingMeters) {
        RenderSteppedSlider_("Spacing (m)", spacingMeters, 0.5f, 50.0f, 0.5f, {2.0f, 4.0f, 8.0f, 16.0f});
    }

    static void RenderGridStepControl_(float& gridStepMeters) {
        RenderSteppedSlider_("Grid step (m)", gridStepMeters, 1.0f, 16.0f, 0.5f, {2.0f, 4.0f, 8.0f, 16.0f});
    }

    // startIndex: 0 when the controls start a line, or their place after the buttons on it.
    // The paint dialog's settings: what comes first is what changes the result most. itemsName is
    // what is placed, such as "props". heightControls: the items can be raised and lowered.
    void RenderPaintOptions_(PropPaintSettings& settings, const char* itemsName, const bool heightControls) const {
        ImGui::TextUnformatted("Mode");
        // Painted ground is a fill or a line like the others: only where the sketch comes from differs.
        constexpr int kOnPaintedGround = 3;
        if (settings.mode == PaintMode::Direct) {
            settings.pickFromGround = false;
        }
        int mode = settings.pickFromGround ? kOnPaintedGround : static_cast<int>(settings.mode);
        ImGui::RadioButton("Direct paint", &mode, static_cast<int>(PaintMode::Direct));
        ImGui::RadioButton("Paint along line", &mode, static_cast<int>(PaintMode::Line));
        ImGui::RadioButton("Paint inside polygon", &mode, static_cast<int>(PaintMode::Polygon));
        const bool hasPaintedGround = director_ && director_->HasPaintedGround();
        ImGui::BeginDisabled(!hasPaintedGround);
        ImGui::RadioButton("Paint on painted ground", &mode, kOnPaintedGround);
        ImGui::EndDisabled();
        PanelUi::Tooltip(hasPaintedGround ? "Click ground that you painted to fill it, or to plant along its edge."
                                          : "First paint some ground in the Terrain Paint tab.");
        if (mode == kOnPaintedGround) {
            if (settings.mode == PaintMode::Direct) {
                SetPaintMode(settings, PaintMode::Polygon);
            }
            settings.pickFromGround = true;
            ImGui::Indent();
            int shape = static_cast<int>(settings.mode);
            ImGui::RadioButton("Fill it", &shape, static_cast<int>(PaintMode::Polygon));
            ImGui::SameLine();
            ImGui::RadioButton("Along its edge", &shape, static_cast<int>(PaintMode::Line));
            ImGui::Unindent();
            SetPaintMode(settings, static_cast<PaintMode>(shape));
        }
        else {
            settings.pickFromGround = false;
            SetPaintMode(settings, static_cast<PaintMode>(mode));
        }

        // Keeps the dialog the same width in every mode.
        char howTo[160];
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 26.0f);
        if (settings.mode == PaintMode::Line) {
            RenderSpacingControl_(settings.spacingMeters);
            ImGui::Checkbox("Align to path direction", &settings.alignToPath);
            ImGui::Checkbox("Random rotation", &settings.randomRotation);
            ImGui::SliderFloat("Lateral jitter (m)", &settings.randomOffset, 0.0f, 5.0f, "%.1f");
            std::snprintf(howTo, sizeof(howTo), "Click to add line points. Press Enter to place the %s. Press Backspace to remove the last point.", itemsName);
        }
        else if (settings.mode == PaintMode::Polygon) {
            RenderPolygonFillControls(settings);
            std::snprintf(howTo, sizeof(howTo), "Click to add polygon corners. Press Enter to fill the polygon with %s. Press Backspace to remove the last corner.", itemsName);
        }
        else {
            std::snprintf(howTo, sizeof(howTo), "Click to place %s. Press Esc to stop and keep the %s that you placed.", itemsName, itemsName);
        }
        if (settings.pickFromGround) {
            std::snprintf(howTo, sizeof(howTo), "Click ground that you painted. Press Enter to place the %s. Click other painted ground to change your choice.", itemsName);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(howTo);
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::TextUnformatted("Rotation");
        int rotation = settings.rotation & 3;
        for (int quarter = 0; quarter < 4; ++quarter) {
            char label[16];
            std::snprintf(label, sizeof(label), "%d deg", quarter * 90);
            if (quarter > 0) {
                ImGui::SameLine();
            }
            ImGui::RadioButton(label, &rotation, quarter);
        }
        settings.rotation = rotation;

        int previewMode = static_cast<int>(settings.previewMode);
        ImGui::Combo("Preview", &previewMode, kPreviewModeLabels, IM_ARRAYSIZE(kPreviewModeLabels));
        settings.previewMode = static_cast<PreviewMode>(previewMode);

        // Rarely changed. What is on still shows while this is closed.
        const bool customHeights = heightControls && settings.sketchHeightMode == SketchHeightMode::Custom;
        char header[80];
        std::snprintf(header, sizeof(header), "%s%s%s###paintGrid", heightControls ? "Grid and height" : "Grid",
                      settings.snapPointsToGrid ? ", snap on" : "", customHeights ? ", custom heights" : "");
        if (ImGui::TreeNode(header)) {
            ImGui::Checkbox("Show grid overlay", &settings.showGrid);
            ImGui::Checkbox("Snap points to grid", &settings.snapPointsToGrid);
            if (!settings.snapPointsToGrid) {
                settings.snapPlacementsToGrid = false;
            }
            ImGui::BeginDisabled(!settings.snapPointsToGrid);
            ImGui::Checkbox("Also snap placements to grid", &settings.snapPlacementsToGrid);
            ImGui::EndDisabled();
            if (!settings.snapPointsToGrid) {
                PanelUi::Tooltip("First turn on Snap points to grid.");
            }
            RenderGridStepControl_(settings.gridStepMeters);
            if (heightControls) {
                if (settings.mode == PaintMode::Line || settings.mode == PaintMode::Polygon) {
                    bool custom = settings.sketchHeightMode == SketchHeightMode::Custom;
                    if (ImGui::Checkbox("Custom node heights", &custom)) {
                        settings.sketchHeightMode = custom ? SketchHeightMode::Custom : SketchHeightMode::Terrain;
                    }
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted("Press [ to lower or ] to raise by 1.0 m. Hold Shift for 5.0 m, or Shift+Alt for 0.1 m.");
                if (settings.mode == PaintMode::Direct) {
                    ImGui::TextUnformatted("Press H in the city to set the absolute reference height.");
                }
                ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }
        ImGui::PopTextWrapPos();
    }

    // Start and Cancel of a paint dialog, also on Enter and Esc. True in the frame Start is chosen.
    [[nodiscard]] static bool RenderStartCancel_() {
        // Not in the frame the dialog opens, as Enter may have opened it, and not while one of its
        // dropdowns or popups has the keys.
        const bool keys = !ImGui::GetIO().WantTextInput && !ImGui::IsWindowAppearing() &&
                          ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);
        const bool start = ImGui::Button("Start") ||
                           (keys && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)));
        PanelUi::Tooltip("You can also press Enter.");
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel") || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
        PanelUi::Tooltip("You can also press Esc.");
        if (start || cancel) {
            ImGui::CloseCurrentPopup();
        }
        return start;
    }

    void RenderPropStripperControls_(const size_t startIndex = 0) const {
        if (!director_) {
            return;
        }

        uint32_t sourceFlags = director_->GetPropStripperSources();
        bool cityEnabled = (sourceFlags & PropStripperInputControl::SourceFlagCity) != 0;
        bool lotEnabled = (sourceFlags & PropStripperInputControl::SourceFlagLot) != 0;
        bool streetEnabled = (sourceFlags & PropStripperInputControl::SourceFlagStreet) != 0;
        bool sourceChanged = false;

        PanelUi::NextInRow(startIndex, PanelUi::CheckboxWidth("City"), PanelUi::kGroupGap);
        sourceChanged = ImGui::Checkbox("City##PropStripCity", &cityEnabled) || sourceChanged;
        PanelUi::NextInRow(1, PanelUi::CheckboxWidth("Lot"));
        sourceChanged = ImGui::Checkbox("Lot##PropStripLot", &lotEnabled) || sourceChanged;
        PanelUi::NextInRow(1, PanelUi::CheckboxWidth("Street"));
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
            PanelUi::NextInRow(1, PanelUi::ButtonWidth("Stop stripping"));
            if (ImGui::SmallButton("Stop stripping")) {
                director_->StopPropStripping();
            }
            PanelUi::Hint(director_->GetPropStripperStatus().c_str());
        }
        else {
            PanelUi::NextInRow(1, PanelUi::ButtonWidth("Strip props"));
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
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (hasAnySource) {
                    ImGui::SetTooltip("Click a prop to remove it.\nPress B to change between single, brush and polygon mode.\nIn brush mode, hold the left mouse button to remove all props in the circle.\nIn polygon mode, click the corners of an area, then press Enter to remove the props in it.\nPress Backspace to remove the last corner.\nPress Ctrl+Z to put back city, lot and street props.\nPress Esc to clear the polygon or to stop.");
                }
                else {
                    ImGui::SetTooltip("Turn on City, Lot or Street first.");
                }
            }
        }
    }

    // startIndex: 0 when the controls start a line, or their place after the buttons on it.
    void RenderFloraStripperControls_(const size_t startIndex = 0) const {
        if (!director_) {
            return;
        }

        if (director_->IsFloraStripping()) {
            PanelUi::NextInRow(startIndex, PanelUi::ButtonWidth("Stop stripping"), PanelUi::kGroupGap);
            if (ImGui::SmallButton("Stop stripping")) {
                director_->StopFloraStripping();
            }
        }
        else {
            PanelUi::NextInRow(startIndex, PanelUi::ButtonWidth("Strip flora"), PanelUi::kGroupGap);
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
        PanelUi::NextInRow(1, PanelUi::CheckboxWidth("Grid"), PanelUi::kGroupGap);
        changed |= ImGui::Checkbox("Grid", &gridView_);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show only the thumbnails.\nMove the mouse over a thumbnail to see its details.\nDouble-click a thumbnail to paint with it.\nRight-click a thumbnail for more options.");
        }
        if (gridView_) {
            PanelUi::NextInRow(1, 6.0f * ImGui::GetFontSize());
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
