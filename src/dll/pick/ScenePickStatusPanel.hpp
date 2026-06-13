#pragma once
#include "ScenePickerInputControl.hpp"
#include "imgui.h"
#include "public/ImGuiPanel.h"
#include "public/ImGuiTexture.h"

class cIGZImGuiService;
class FloraRepository;
class LotRepository;
class PropRepository;

// Cursor-following readout for the scene picker: what is hovered (with a
// thumbnail where one is available), and the position within the stack when
// overlapping candidates can be cycled.
class ScenePickStatusPanel final : public ImGuiPanel {
public:
    void OnRender() override;
    void OnShutdown() override {
        picker_ = nullptr;
        ClearThumbnail_();
        imguiService_ = nullptr;
        props_ = nullptr;
        flora_ = nullptr;
        lots_ = nullptr;
    }

    void SetPicker(ScenePickerInputControl* picker) { picker_ = picker; }
    void SetImGuiService(cIGZImGuiService* imguiService) { imguiService_ = imguiService; }
    void SetRepositories(PropRepository* props, FloraRepository* flora, LotRepository* lots) {
        props_ = props;
        flora_ = flora;
        lots_ = lots;
    }

private:
    enum class ThumbnailKind : uint8_t {
        None = 0,
        DecalTexture,  // keyed by texture instance ID
        Prop,          // keyed by group/instance GI key
        Flora,         // keyed by group/instance GI key
        Building,      // keyed by group/instance GI key of the lot's building
    };

    void RenderHoveredThumbnail_(const ScenePickResult& hovered);
    void ClearThumbnail_();

    ScenePickerInputControl* picker_ = nullptr;
    cIGZImGuiService* imguiService_ = nullptr;
    PropRepository* props_ = nullptr;
    FloraRepository* flora_ = nullptr;
    LotRepository* lots_ = nullptr;

    // Single-entry thumbnail cache for the currently hovered item; failures
    // are remembered to avoid re-loading every frame.
    ImGuiTexture thumbnail_{};
    ImVec2 thumbnailSourceSize_{0.0f, 0.0f};
    ThumbnailKind thumbnailKind_ = ThumbnailKind::None;
    uint64_t thumbnailKey_ = 0;
    bool thumbnailValid_ = false;
    uint32_t lastDeviceGeneration_ = 0;
};
