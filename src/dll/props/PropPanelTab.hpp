#pragma once
#include <unordered_map>
#include <vector>
#include "../common/PanelTab.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "PropFilterHelper.hpp"
#include "PropPainterInputControl.hpp"
#include "public/ImGuiTexture.h"

class PropPanelTab : public PanelTab {
public:
    PropPanelTab(SC4PlopAndPaintDirector* director,
                 LotRepository* lots, PropRepository* props, FavoritesRepository* favorites,
                 cIGZImGuiService* imguiService)
        : PanelTab(director, lots, props, favorites, imguiService) {
        pendingPaint_.settings.showGrid = director->GetDefaultShowGridOverlay();
        pendingPaint_.settings.snapPointsToGrid = director->GetDefaultSnapPointsToGrid();
        pendingPaint_.settings.snapPlacementsToGrid = director->GetDefaultSnapPlacementsToGrid();
        pendingPaint_.settings.gridStepMeters = director->GetDefaultGridStepMeters();
        pendingPaint_.settings.previewMode = director->GetDefaultPropPreviewMode();
    }

    ~PropPanelTab() override = default;

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnShutdown() override { thumbnailCache_.Clear(); }
    void OnDeviceReset(uint32_t deviceGeneration) override;

private:
    ImGuiTexture LoadPropTexture_(uint64_t propKey);

    void RenderFilterUI_();
    void RenderTable_();
    void RebuildPropViewsCache_();
    void EnsureFilteredPropsCache_();

    void RenderTableInternal_(const std::vector<PropView>& filteredProps,
                              const std::unordered_set<uint64_t>& favorites);

    [[nodiscard]] bool HasPropTooltipContent_(const Prop& prop) const;
    [[nodiscard]] bool RenderPropPills_(const Prop& prop, bool startOnNewLine) const;
    void RenderPropTooltip_(const Prop& prop) const;
    void RenderFavButton_(const Prop& prop) const;
    void RenderRotationModal_();

    static uint64_t MakePropKey_(const Prop& prop);

private:
    ThumbnailCache<uint64_t> thumbnailCache_;
    uint32_t lastDeviceGeneration_{0};
    bool texturesLoaded_ = false;

    struct PendingPaintState {
        uint32_t propId = 0;
        std::string propName;
        PropPaintSettings settings{};
        bool open = false;
    };

    PendingPaintState pendingPaint_{};
    PropFilterHelper filterHelper_;
    std::vector<PropFilterHelper::SortSpec> sortSpecs_ = {
        {PropFilterHelper::SortColumn::Name, false}
    };
    std::vector<PropView> allPropViewsCache_{};
    std::vector<PropView> filteredPropsCache_{};
    size_t cachedPropCount_{0};
    bool filteredPropsDirty_{true};
};
