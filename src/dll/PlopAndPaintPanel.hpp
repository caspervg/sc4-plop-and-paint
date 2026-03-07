#pragma once
#include <memory>
#include <vector>
#include "SC4PlopAndPaintDirector.hpp"
#include "common/PanelTab.hpp"
#include "public/ImGuiTexture.h"

class FloraRepository;
class LotRepository;
class PropRepository;
class FavoritesRepository;

class PlopAndPaintPanel final : public ImGuiPanel {
public:
    PlopAndPaintPanel(SC4PlopAndPaintDirector* director,
                 LotRepository* lots,
                 PropRepository* props,
                 FloraRepository* flora,
                 FavoritesRepository* favorites,
                 cIGZImGuiService* imguiService);
    void OnInit() override {}
    void OnRender() override;
    void OnShutdown() override { Shutdown(); }
    void SetOpen(bool open);
    [[nodiscard]] bool IsOpen() const;
    void Shutdown() const;

private:
    SC4PlopAndPaintDirector* director_;
    cIGZImGuiService* imguiService_;
    bool isOpen_{false};
    uint32_t lastDeviceGeneration_{0};
    std::vector<std::unique_ptr<PanelTab>> tabs_;
};
