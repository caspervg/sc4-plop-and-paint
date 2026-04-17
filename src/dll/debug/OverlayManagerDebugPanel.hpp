#pragma once

#include <memory>

#include "public/ImGuiPanel.h"

class SC4PlopAndPaintDirector;

namespace DebugUi
{
    class OverlayManagerDebugPanelState;

    class OverlayManagerDebugPanel final : public ImGuiPanel
    {
    public:
        explicit OverlayManagerDebugPanel(SC4PlopAndPaintDirector* director);
        ~OverlayManagerDebugPanel() override;

        void OnInit() override;
        void OnRender() override;
        void OnShutdown() override;

    private:
        SC4PlopAndPaintDirector* director_;
        std::unique_ptr<OverlayManagerDebugPanelState> state_;
    };
}
