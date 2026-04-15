#pragma once

#include <string>

#include "../common/PanelTab.hpp"
#include "../thumbnail/ThumbnailCache.hpp"
#include "PropPainterInputControl.hpp"

class FamiliesPanelTab : public PanelTab {
public:
    FamiliesPanelTab(SC4PlopAndPaintDirector* director,
                     LotRepository* lots, PropRepository* props, FavoritesRepository* favorites,
                     cIGZImGuiService* imguiService)
        : PanelTab(director, lots, props, favorites, imguiService) {
        familyPaintDefaults_.showGrid = director->GetDefaultShowGridOverlay();
        familyPaintDefaults_.snapPointsToGrid = director->GetDefaultSnapPointsToGrid();
        familyPaintDefaults_.snapPlacementsToGrid = director->GetDefaultSnapPlacementsToGrid();
        familyPaintDefaults_.gridStepMeters = director->GetDefaultGridStepMeters();
        familyPaintDefaults_.previewMode = director->GetDefaultPropPreviewMode();
        pendingPaint_.settings.showGrid = director->GetDefaultShowGridOverlay();
        pendingPaint_.settings.snapPointsToGrid = director->GetDefaultSnapPointsToGrid();
        pendingPaint_.settings.snapPlacementsToGrid = director->GetDefaultSnapPlacementsToGrid();
        pendingPaint_.settings.gridStepMeters = director->GetDefaultGridStepMeters();
        pendingPaint_.settings.previewMode = director->GetDefaultPropPreviewMode();
    }

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override { thumbnailCache_.Clear(); }

private:
    ImGuiTexture LoadPropTexture_(uint64_t propKey) const;
    void RenderNewFamilyPopup_();
    void RenderDeleteFamilyPopup_(size_t userFamilyIndex);
    void QueuePaintForSelectedFamily_();
    void RenderPaintOptionsPopup_();
    bool StartPaintingWithSelectedFamily_();

    // Returns the active PropFamily* given the combined index (auto first, then user).
    [[nodiscard]] const PropFamily* GetSelectedFamily_() const;
    [[nodiscard]] bool IsSelectedAutoFamily_() const;

private:
    ThumbnailCache<uint64_t> thumbnailCache_{};
    uint32_t lastDeviceGeneration_{0};
    size_t selectedFamilyIndex_{0};
    bool newFamilyPopupOpen_ = false;
    bool deleteFamilyPopupOpen_ = false;
    char newFamilyName_[128] = {};
    std::string nameFilter_{};
    std::string iidFilter_{};
    PropPaintSettings familyPaintDefaults_{};

    struct PendingFamilyPaintState {
        uint32_t fallbackPropId = 0;
        RecentPaintEntry::SourceKind sourceKind{RecentPaintEntry::SourceKind::PropAutoFamily};
        uint64_t sourceId = 0;
        std::string familyName;
        PropPaintSettings settings{};
        bool open = false;
    };

    PendingFamilyPaintState pendingPaint_{};
};
