#pragma once

#include <string>

#include "PanelTab.hpp"
#include "PropPainterInputControl.hpp"
#include "ThumbnailCache.hpp"

class FamiliesPanelTab : public PanelTab {
public:
    FamiliesPanelTab(SC4AdvancedLotPlopDirector* director,
                     LotRepository* lots, PropRepository* props, FavoritesRepository* favorites,
                     cIGZImGuiService* imguiService)
        : PanelTab(director, lots, props, favorites, imguiService) {}

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override { thumbnailCache_.Clear(); }

private:
    ImGuiTexture LoadPropTexture_(uint64_t propKey);
    void RenderNewFamilyPopup_();
    void RenderDeleteFamilyPopup_(size_t userFamilyIndex);
    bool StartPaintingWithSelectedFamily_(PropPaintMode mode);
    static std::string PropDisplayName_(const Prop& prop);

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
    PropPaintSettings familyPaintDefaults_{};
};
