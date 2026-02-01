#pragma once
#include <unordered_map>
#include <vector>
#include "FilterableTablePanel.hpp"
#include "PanelTab.hpp"
#include "PropPainterInputControl.hpp"
#include "PropFilterHelper.hpp"
#include "public/ImGuiTexture.h"

class PropPanelTab : public FilterableTablePanel, public PanelTab {
public:
    explicit PropPanelTab(SC4AdvancedLotPlopDirector* director, cIGZImGuiService* imguiService)
        : PanelTab(director, imguiService) {}

    ~PropPanelTab() override = default;

    [[nodiscard]] const char* GetTabName() const override;
    void OnRender() override;
    void OnInit() override {}
    void OnShutdown() override {}
    void OnDeviceReset(uint32_t deviceGeneration) override;

private:
    void LoadIconTexture_(uint64_t propKey, const Prop& prop);

    void RenderFilterUI_() override;
    void RenderTable_() override;

    void RenderTableInternal_(const std::vector<PropView>& filteredProps,
                              const std::unordered_set<uint64_t>& favorites);

    void RenderFavButton_(const Prop& prop) const;
    void RenderRotationModal_();

private:
    std::unordered_map<uint64_t, ImGuiTexture> iconCache_;
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
};
