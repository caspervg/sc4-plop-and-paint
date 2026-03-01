#pragma once

#include <cRZCOMDllDirector.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include "cIGZCommandServer.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZWin.h"
#include "cISC4App.h"
#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "cRZAutoRefCount.h"
#include "cRZMessage2COMDirector.h"
#include "cRZMessage2COMDirector.h"
#include "GZServPtrs.h"
#include "imgui.h"
#include "PropPainterInputControl.hpp"
#include "../shared/entities.hpp"
#include "public/cIGZImGuiService.h"
#include "public/cIGZDrawService.h"
#include "public/ImGuiPanel.h"
#include "public/ImGuiPanelAdapter.h"
#include "public/ImGuiServiceIds.h"

class LotPlopPanel;
class LotRepository;
class PropRepository;
class FavoritesRepository;
struct PropPaintSettings;
class cIGZS3DCameraService;

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
    bool StartPropPainting(uint32_t propId, const PropPaintSettings& settings, const std::string& name);
    bool SwitchPropPaintingTarget(uint32_t propId, const std::string& name);
    void StopPropPainting();
    [[nodiscard]] bool IsPropPainting() const;
    void SetLotPlopPanelVisible(bool visible);

private:
    void PostCityInit_(const cIGZMessage2Standard* pStandardMsg);
    void PreCityShutdown_(cIGZMessage2Standard* pStandardMsg);
    void ToggleLotPlopPanel_();
    bool RegisterLotPlopShortcut_();
    void UnregisterLotPlopShortcut_();
    static std::filesystem::path GetUserPluginsPath_();
    static void DrawOverlayCallback_(DrawServicePass pass, bool begin, void* pThis);

private:
    cIGZImGuiService* imguiService_ = nullptr;
    cIGZDrawService* drawService_ = nullptr;
    cRZAutoRefCount<cISC4City> pCity_;
    cISC4View3DWin* pView3D_ = nullptr;
    cRZAutoRefCount<cIGZMessageServer2> pMS2_;
    cIGZS3DCameraService* cameraService_ = nullptr;

    std::unique_ptr<LotRepository>       lotRepository_;
    std::unique_ptr<PropRepository>      propRepository_;
    std::unique_ptr<FavoritesRepository> favoritesRepository_;

    bool panelRegistered_{false};
    bool panelVisible_{false};
    bool shortcutRegistered_{false};
    std::unique_ptr<LotPlopPanel> panel_;
    cRZAutoRefCount<PropPainterInputControl> propPainterControl_;
    bool propPainting_{false};
    uint32_t drawCallbackToken_{0};
};
