#pragma once

#include <string>

#include "PanelTab.hpp"
#include "PropPainterInputControl.hpp"
#include "ThumbnailCache.hpp"

class PalettesPanelTab : public PanelTab {
public:
    explicit PalettesPanelTab(SC4AdvancedLotPlopDirector* director, cIGZImGuiService* imguiService)
        : PanelTab(director, imguiService) {}

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnDeviceReset(uint32_t deviceGeneration) override;
    void OnShutdown() override { thumbnailCache_.Clear(); }

private:
    ImGuiTexture LoadPropTexture_(uint64_t propKey);
    const Prop* FindPropByInstanceID_(uint32_t propID) const;
    void RenderNewPalettePopup_();
    void RenderDeletePalettePopup_(size_t index);
    bool StartPaintingWithActivePalette_(PropPaintMode mode);
    static std::string PropDisplayName_(const Prop& prop);

private:
    ThumbnailCache<uint64_t> thumbnailCache_{};
    uint32_t lastDeviceGeneration_{0};
    bool newPalettePopupOpen_ = false;
    bool deletePalettePopupOpen_ = false;
    char newPaletteName_[128] = {};
    PropPaintSettings palettePaintDefaults_{};
};
