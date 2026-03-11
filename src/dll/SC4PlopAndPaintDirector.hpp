#pragma once

#include <cRZCOMDllDirector.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include "../shared/entities.hpp"
#include "RecentPaintHistory.hpp"
#include "GZServPtrs.h"
#include "cIGZCommandServer.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZWin.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "cRZAutoRefCount.h"
#include "cRZMessage2COMDirector.h"
#include "flora/FloraPainterInputControl.hpp"
#include "imgui.h"
#include "paint/PaintStatusPanel.hpp"
#include "props/PropPainterInputControl.hpp"
#include "props/PropStripperInputControl.hpp"
#include "public/ImGuiPanel.h"
#include "public/ImGuiPanelAdapter.h"
#include "public/ImGuiServiceIds.h"
#include "public/cIGZDrawService.h"
#include "public/cIGZImGuiService.h"
#include "utils/Settings.h"

class PlopAndPaintPanel;
class FloraRepository;
class LotRepository;
class PropRepository;
class FavoritesRepository;
class cIGZS3DCameraService;
class RecentSwapPanel;

static constexpr uint32_t kSC4MessagePostCityInit = 0x26D31EC1;
static constexpr uint32_t kSC4MessagePreCityShutdown = 0x26D31EC2;

class SC4PlopAndPaintDirector final : public cRZMessage2COMDirector
{
public:
    SC4PlopAndPaintDirector();
    ~SC4PlopAndPaintDirector() override;

    [[nodiscard]] uint32_t GetDirectorID() const override;
    bool OnStart(cIGZCOM* pCOM) override;

    bool PreFrameWorkInit() override;
    bool PreAppInit() override;
    bool PostAppInit() override;
    bool PreAppShutdown() override;
    bool PostAppShutdown() override;
    bool PostSystemServiceShutdown() override;
    bool AbortiveQuit() override;
    bool OnInstall() override;
    bool DoMessage(cIGZMessage2* pMsg) override;

    void TriggerLotPlop(uint32_t lotInstanceId) const;
    bool StartPropPainting(uint32_t propId, const PropPaintSettings& settings, const std::string& name,
                           const std::optional<RecentPaintSource>& source = std::nullopt);
    bool SwitchPropPaintingTarget(uint32_t propId, const std::string& name,
                                  const std::optional<RecentPaintSource>& source = std::nullopt);
    void StopPropPainting();
    [[nodiscard]] bool IsPropPainting() const;
    bool StartFloraPainting(uint32_t floraTypeId, const PropPaintSettings& settings, const std::string& name,
                            const std::optional<RecentPaintSource>& source = std::nullopt);
    void StopFloraPainting();
    [[nodiscard]] bool IsFloraPainting() const;
    bool StartPropStripping();
    void StopPropStripping();
    [[nodiscard]] bool IsPropStripping() const;
    [[nodiscard]] BasePainterInputControl* GetActivePainterControl() const;
    [[nodiscard]] const RecentPaintHistory& GetRecentPaintHistory() const;
    bool ActivateRecentPaint(size_t index);
    [[nodiscard]] bool GetDefaultShowGridOverlay() const noexcept;
    [[nodiscard]] bool GetDefaultSnapPointsToGrid() const noexcept;
    [[nodiscard]] bool GetDefaultSnapPlacementsToGrid() const noexcept;
    [[nodiscard]] float GetDefaultGridStepMeters() const noexcept;
    [[nodiscard]] PreviewMode GetDefaultPropPreviewMode() const noexcept;
    [[nodiscard]] ImU32 GetThumbnailBackgroundColor() const noexcept;
    [[nodiscard]] ImU32 GetThumbnailBorderColor() const noexcept;
    void SetLotPlopPanelVisible(bool visible);

private:
    void PostCityInit_(const cIGZMessage2Standard* pStandardMsg);
    void PreCityShutdown_(cIGZMessage2Standard* pStandardMsg);
    void ToggleLotPlopPanel_();
    bool RegisterLotPlopShortcut_();
    void UnregisterLotPlopShortcut_();
    static std::filesystem::path GetUserPluginsPath_();
    static void DrawOverlayCallback_(DrawServicePass pass, bool begin, void* pThis);
    void ProcessPendingToolActions_();
    void UpdatePaintPanels_();
    void SyncRecentPaintsCache_();
    void PersistRecentPaints_();
    bool PrepareForPaintSwitch_(BasePainterInputControl* control, bool& isPaintingFlag);
    void ApplySwitchPolicy_(BasePainterInputControl* control);
    [[nodiscard]] RecentPaintEntry BuildRecentPaintEntry_(RecentPaintEntry::Kind kind,
                                                          uint32_t typeId,
                                                          const PropPaintSettings& settings,
                                                          const std::string& name,
                                                          const std::optional<RecentPaintSource>& source) const;

private:
    cIGZImGuiService* imguiService_ = nullptr;
    cIGZDrawService* drawService_ = nullptr;
    cRZAutoRefCount<cISC4City> pCity_;
    cISC4View3DWin* pView3D_ = nullptr;
    cRZAutoRefCount<cIGZMessageServer2> pMS2_;
    cIGZS3DCameraService* cameraService_ = nullptr;

    std::unique_ptr<LotRepository>       lotRepository_;
    std::unique_ptr<PropRepository>      propRepository_;
    std::unique_ptr<FloraRepository>     floraRepository_;
    std::unique_ptr<FavoritesRepository> favoritesRepository_;

    bool panelRegistered_{false};
    bool panelVisible_{false};
    bool shortcutRegistered_{false};
    std::unique_ptr<PlopAndPaintPanel> panel_;
    cRZAutoRefCount<PropPainterInputControl>  propPainterControl_;
    bool propPainting_{false};
    cRZAutoRefCount<FloraPainterInputControl>  floraPlacerControl_;
    bool floraPainting_{false};
    cRZAutoRefCount<PropStripperInputControl> propStripperControl_;
    bool propStripping_{false};
    std::unique_ptr<PaintStatusPanel> statusPanel_;
    bool statusPanelRegistered_{false};
    std::unique_ptr<RecentSwapPanel> swapPanel_;
    bool swapPanelRegistered_{false};
    RecentPaintHistory recentPaints_{};
    bool enableRecentPaints_{true};
    PaintSwitchPolicy paintSwitchPolicy_{PaintSwitchPolicy::KeepPending};
    bool drawOverlayEnabled_{true};
    uint32_t drawCallbackToken_{0};
    PreviewMode defaultPropPreviewMode_ = PreviewMode::Outline;
    bool defaultShowGridOverlay_ = true;
    bool defaultSnapPointsToGrid_ = false;
    bool defaultSnapPlacementsToGrid_ = false;
    float defaultGridStepMeters_ = 16.0f;
    ImU32 thumbnailBackgroundColor_ = IM_COL32(0, 0, 0, 0);
    ImU32 thumbnailBorderColor_ = IM_COL32(0, 0, 0, 0);
};
