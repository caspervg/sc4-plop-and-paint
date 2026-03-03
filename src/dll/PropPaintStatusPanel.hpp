#pragma once
#include "PropPainterInputControl.hpp"
#include "public/ImGuiPanel.h"

class PropPaintStatusPanel final : public ImGuiPanel {
public:
    void OnRender() override;
    void OnShutdown() override {
        activeControl_ = nullptr;
        visible_ = false;
    }

    void SetActiveControl(PropPainterInputControl* control);
    void SetVisible(bool visible);

private:
    PropPainterInputControl* activeControl_ = nullptr;
    bool visible_ = false;
};
