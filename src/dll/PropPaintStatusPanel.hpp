#pragma once
#include "BasePainterInputControl.hpp"
#include "public/ImGuiPanel.h"

class PropPaintStatusPanel final : public ImGuiPanel {
public:
    void OnRender() override;
    void OnShutdown() override {
        activeControl_ = nullptr;
        visible_ = false;
    }

    void SetActiveControl(BasePainterInputControl* control);
    void SetVisible(bool visible);

private:
    BasePainterInputControl* activeControl_ = nullptr;
    bool visible_ = false;
};
