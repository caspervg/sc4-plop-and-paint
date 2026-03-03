// ReSharper disable CppDFAConstantConditions
// ReSharper disable CppDFAUnreachableCode
#include "SC4PlopAndPaintDirector.hpp"

#include <cIGZFrameWork.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "cGZPersistResourceKey.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWinKeyAccelerator.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cRZBaseVariant.h"
#include "FavoritesRepository.hpp"
#include "LotPlopPanel.hpp"
#include "LotRepository.hpp"
#include "PropPainterInputControl.hpp"
#include "PropStripperInputControl.hpp"
#include "PropRepository.hpp"
#include "Utils.hpp"
#include "utils/Logger.h"
#include "utils/Settings.h"
#include "public/cIGZS3DCameraService.h"
#include "public/S3DCameraServiceIds.h"

namespace {
    constexpr auto kSC4AdvancedLotPlopDirectorID = 0xE5C2B9A7u;

    constexpr auto kGZWin_WinSC4App = 0x6104489Au;
    constexpr auto kGZWin_SC4View3DWin = 0x9a47b417u;

    constexpr auto kLotPlopPanelId = 0xCA500001u;
    constexpr auto kToggleLotPlopWindowShortcutID = 0x9F21C3A1u;
    constexpr auto kKeyConfigType = 0xA2E3D533u;
    constexpr auto kKeyConfigGroup = 0x8F1E6D69u;
    constexpr auto kKeyConfigInstance = 0x5CBCFBF8u;
}

SC4PlopAndPaintDirector::SC4PlopAndPaintDirector()
    : imguiService_(nullptr)
      , drawService_(nullptr)
      , pView3D_(nullptr)
      , panelRegistered_(false) {}

SC4PlopAndPaintDirector::~SC4PlopAndPaintDirector() = default;

uint32_t SC4PlopAndPaintDirector::GetDirectorID() const {
    return kSC4AdvancedLotPlopDirectorID;
}

bool SC4PlopAndPaintDirector::OnStart(cIGZCOM* pCOM) {
    cRZMessage2COMDirector::OnStart(pCOM);

    if (auto* framework = RZGetFrameWork()) {
        framework->AddHook(this);
    }
    return true;
}

bool SC4PlopAndPaintDirector::PreFrameWorkInit() { return true; }
bool SC4PlopAndPaintDirector::PreAppInit() { return true; }

bool SC4PlopAndPaintDirector::PostAppInit() {
    const auto pluginsPath = GetUserPluginsPath_();
    const auto logPath = pluginsPath.parent_path();
    const auto settingsPath = pluginsPath / "SC4PlopAndPaint.ini";

    Logger::Initialize("SC4PlopAndPaint", logPath.string(), false);
    Settings settings;
    settings.Load(settingsPath);
    defaultPropPreviewMode_ = settings.GetDefaultPropPreviewMode();
    defaultShowGridOverlay_ = settings.GetDefaultShowGridOverlay();
    defaultSnapPointsToGrid_ = settings.GetDefaultSnapPointsToGrid();
    defaultSnapPlacementsToGrid_ = settings.GetDefaultSnapPlacementsToGrid();
    defaultGridStepMeters_ = settings.GetDefaultGridStepMeters();
    Logger::Shutdown();
    Logger::Initialize("SC4PlopAndPaint", logPath.string(), settings.GetLogToFile());
    Logger::SetLevel(settings.GetLogLevel());

    LOG_INFO("SC4AdvancedLotPlopDirector initialized");
    LOG_INFO("Using settings file: {}", settingsPath.string());
    LOG_INFO("Applied logging settings: level={}, file={}",
             spdlog::level::to_string_view(settings.GetLogLevel()), settings.GetLogToFile());

    cIGZMessageServer2Ptr pMS2;
    if (pMS2) {
        pMS2->AddNotification(this, kSC4MessagePostCityInit);
        pMS2->AddNotification(this, kSC4MessagePreCityShutdown);
        pMS2_ = pMS2;
        LOG_INFO("Registered for city messages");
    }

    if (mpFrameWork && mpFrameWork->GetSystemService(kImGuiServiceID, GZIID_cIGZImGuiService,
                                                     reinterpret_cast<void**>(&imguiService_))) {
        LOG_INFO("Acquired ImGui service");

        if (mpFrameWork->GetSystemService(kS3DCameraServiceID, GZIID_cIGZS3DCameraService,
                                          reinterpret_cast<void**>(&cameraService_))) {
            LOG_INFO("Acquired S3D camera service");
        }
        else {
            LOG_WARN("S3D camera service not available");
        }

        if (settings.GetEnableDrawOverlay() &&
            mpFrameWork->GetSystemService(kDrawServiceID, GZIID_cIGZDrawService,
                                          reinterpret_cast<void**>(&drawService_))) {
            LOG_INFO("Acquired draw service");
            if (!drawService_->RegisterDrawPassCallback(
                DrawServicePass::PreDynamic,
                &DrawOverlayCallback_,
                this,
                &drawCallbackToken_)) {
                LOG_WARN("Failed to register draw pass callback");
            }
        }
        else if (!settings.GetEnableDrawOverlay()) {
            LOG_INFO("Draw overlay disabled in settings");
        }
        else {
            LOG_WARN("Draw service not available");
        }

        lotRepository_       = std::make_unique<LotRepository>();
        propRepository_      = std::make_unique<PropRepository>();
        favoritesRepository_ = std::make_unique<FavoritesRepository>(*propRepository_);

        lotRepository_->Load();
        propRepository_->Load();
        favoritesRepository_->Load();

        panel_ = std::make_unique<LotPlopPanel>(
            this,
            lotRepository_.get(),
            propRepository_.get(),
            favoritesRepository_.get(),
            imguiService_);

        const ImGuiPanelDesc desc = ImGuiPanelAdapter<LotPlopPanel>::MakeDesc(
            panel_.get(), kLotPlopPanelId, 100, true
        );

        if (imguiService_->RegisterPanel(desc)) {
            panelRegistered_ = true;
            panelVisible_ = false;
            panel_->SetOpen(false);
            LOG_INFO("Registered ImGui panel");
        }
    }
    else {
        LOG_WARN("ImGui service not found or not available");
    }

    return true;
}

bool SC4PlopAndPaintDirector::PreAppShutdown() { return true; }

bool SC4PlopAndPaintDirector::PostAppShutdown() {
    UnregisterLotPlopShortcut_();

    if (pMS2_) {
        pMS2_->RemoveNotification(this, kSC4MessagePostCityInit);
        pMS2_->RemoveNotification(this, kSC4MessagePreCityShutdown);
        pMS2_.Reset();
    }

    if (auto* framework = RZGetFrameWork()) {
        framework->RemoveHook(this);
    }

    // Destroy objects that may call ImGuiService first.
    if (propStripperControl_) {
        StopPropStripping();
        propStripperControl_->SetCity(nullptr);
        propStripperControl_->Shutdown();
        propStripperControl_.Reset();
    }

    if (propPainterControl_) {
        StopPropPainting();
        propPainterControl_->SetCity(nullptr);
        propPainterControl_->Shutdown();
        propPainterControl_.Reset();
    }

    if (imguiService_ && panelRegistered_) {
        SetLotPlopPanelVisible(false);
        imguiService_->UnregisterPanel(kLotPlopPanelId);
    }
    panelRegistered_ = false;
    if (panel_) {
        panel_->Shutdown(); // Release textures while the ImGui service is still alive
    }
    panel_.reset();

    favoritesRepository_.reset();
    propRepository_.reset();
    lotRepository_.reset();

    if (drawService_ && drawCallbackToken_ != 0) {
        drawService_->UnregisterDrawPassCallback(drawCallbackToken_);
        drawCallbackToken_ = 0;
    }

    if (imguiService_) {
        imguiService_->Release();
        imguiService_ = nullptr;
    }

    if (drawService_) {
        drawService_->Release();
        drawService_ = nullptr;
    }

    if (cameraService_) {
        cameraService_->Release();
        cameraService_ = nullptr;
    }

    Logger::Shutdown();

    return true;
}

bool SC4PlopAndPaintDirector::PostSystemServiceShutdown() { return true; }

bool SC4PlopAndPaintDirector::AbortiveQuit() { return true; }

bool SC4PlopAndPaintDirector::OnInstall() { return true; }

bool SC4PlopAndPaintDirector::DoMessage(cIGZMessage2* pMsg) {
    const auto pStandardMsg = static_cast<cIGZMessage2Standard*>(pMsg);
    switch (pStandardMsg->GetType()) {
    case kSC4MessagePostCityInit: PostCityInit_(pStandardMsg);
        break;
    case kSC4MessagePreCityShutdown: PreCityShutdown_(pStandardMsg);
        break;
    case kToggleLotPlopWindowShortcutID: ToggleLotPlopPanel_();
        break;
    default: break;
    }
    return true;
}

void SC4PlopAndPaintDirector::TriggerLotPlop(uint32_t lotInstanceId) const {
    if (!pView3D_) {
        LOG_WARN("Cannot plop: View3D not available (city not loaded?)");
        return;
    }

    cIGZCommandServerPtr pCmdServer;
    if (!pCmdServer) {
        LOG_WARN("Cannot plop: Command server not available");
        return;
    }

    cIGZCommandParameterSet* pCmd1 = nullptr;
    cIGZCommandParameterSet* pCmd2 = nullptr;

    if (!pCmdServer->CreateCommandParameterSet(&pCmd1) || !pCmd1 ||
        !pCmdServer->CreateCommandParameterSet(&pCmd2) || !pCmd2) {
        if (pCmd1) pCmd1->Release();
        if (pCmd2) pCmd2->Release();
        LOG_ERROR("Failed to create command parameter sets");
        return;
    }

    // Create a fake variant in pCmd1, this will get clobbered in AppendParameter anyway
    cRZBaseVariant dummyVariant;
    dummyVariant.SetValUint32(0);
    pCmd1->AppendParameter(dummyVariant);

    // Get the game's internal variant and patch it directly
    cIGZVariant* storedParam = pCmd1->GetParameter(0);
    if (storedParam) {
        storedParam->SetValUint32(lotInstanceId);
    }

    // Trigger lot plop command (0xec3e82f8 is the lot plop command ID)
    pView3D_->ProcessCommand(0xec3e82f8, *pCmd1, *pCmd2);

    LOG_INFO("Triggered lot plop for instance ID: 0x{:08X}", lotInstanceId);

    pCmd1->Release();
    pCmd2->Release();
}

bool SC4PlopAndPaintDirector::StartPropPainting(uint32_t propId, const PropPaintSettings& settings,
                                                   const std::string& name) {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start prop painting: city or view not available");
        return false;
    }

    if (!propPainterControl_) {
        auto* control = new PropPainterInputControl();
        propPainterControl_ = control;
        if (!propPainterControl_) {
            LOG_ERROR("Failed to allocate PropPainterInputControl");
            return false;
        }

        if (!propPainterControl_->Init()) {
            LOG_ERROR("Failed to initialize PropPainterInputControl");
            propPainterControl_.Reset();
            return false;
        }
    }

    propPainterControl_->SetCity(pCity_);
    propPainterControl_->SetWindow(pView3D_->AsIGZWin());
    propPainterControl_->SetCameraService(cameraService_);
    propPainterControl_->SetPropRepository(propRepository_.get());
    propPainterControl_->SetOnCancel([this]() {
        if (pView3D_ && propPainterControl_ &&
            pView3D_->GetCurrentViewInputControl() == propPainterControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        propPainting_ = false;
        LOG_INFO("Stopped prop painting");
    });

    propPainterControl_->SetPropToPaint(propId, settings, name);
    if (!pView3D_->SetCurrentViewInputControl(
        propPainterControl_,
        cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        LOG_WARN("Failed to set prop painter as current view input control");
        return false;
    }

    propPainting_ = true;
    LOG_INFO("Started prop painting: 0x{:08X}, rotation {}", propId, settings.rotation);
    return true;
}

bool SC4PlopAndPaintDirector::SwitchPropPaintingTarget(uint32_t propId, const std::string& name) {
    if (!propPainterControl_ || !propPainting_ || !pView3D_) {
        return false;
    }
    if (pView3D_->GetCurrentViewInputControl() != propPainterControl_) {
        return false;
    }

    const auto& settings = propPainterControl_->GetSettings();
    return StartPropPainting(propId, settings, name);
}

void SC4PlopAndPaintDirector::StopPropPainting() {
    if (pView3D_ && propPainterControl_) {
        if (pView3D_->GetCurrentViewInputControl() == propPainterControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }

    propPainting_ = false;
    LOG_INFO("Stopped prop painting");
}

bool SC4PlopAndPaintDirector::IsPropPainting() const {
    return propPainting_;
}

bool SC4PlopAndPaintDirector::StartPropStripping() {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start prop stripping: city or view not available");
        return false;
    }

    if (propPainting_) {
        StopPropPainting();
    }

    if (!propStripperControl_) {
        auto* control = new PropStripperInputControl();
        propStripperControl_ = control;
        if (!propStripperControl_->Init()) {
            LOG_ERROR("Failed to initialize PropStripperInputControl");
            propStripperControl_.Reset();
            return false;
        }
    }

    propStripperControl_->SetCity(pCity_);
    propStripperControl_->SetWindow(pView3D_->AsIGZWin());
    propStripperControl_->SetOnCancel([this]() {
        if (pView3D_ && propStripperControl_ &&
            pView3D_->GetCurrentViewInputControl() == propStripperControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        propStripping_ = false;
        LOG_INFO("Stopped prop stripping");
    });

    if (!pView3D_->SetCurrentViewInputControl(
        propStripperControl_,
        cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        LOG_WARN("Failed to set prop stripper as current view input control");
        return false;
    }

    propStripping_ = true;
    LOG_INFO("Started prop stripping");
    return true;
}

void SC4PlopAndPaintDirector::StopPropStripping() {
    if (pView3D_ && propStripperControl_) {
        if (pView3D_->GetCurrentViewInputControl() == propStripperControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }
    propStripping_ = false;
    LOG_INFO("Stopped prop stripping");
}

bool SC4PlopAndPaintDirector::IsPropStripping() const {
    return propStripping_;
}

bool SC4PlopAndPaintDirector::GetDefaultShowGridOverlay() const noexcept {
    return defaultShowGridOverlay_;
}

bool SC4PlopAndPaintDirector::GetDefaultSnapPointsToGrid() const noexcept {
    return defaultSnapPointsToGrid_;
}

bool SC4PlopAndPaintDirector::GetDefaultSnapPlacementsToGrid() const noexcept {
    return defaultSnapPlacementsToGrid_;
}

float SC4PlopAndPaintDirector::GetDefaultGridStepMeters() const noexcept {
    return defaultGridStepMeters_;
}

PropPreviewMode SC4PlopAndPaintDirector::GetDefaultPropPreviewMode() const noexcept {
    return defaultPropPreviewMode_;
}

void SC4PlopAndPaintDirector::DrawOverlayCallback_(const DrawServicePass pass, const bool begin, void* pThis) {
    if (pass != DrawServicePass::PreDynamic || begin) {
        return;
    }

    auto* director = static_cast<SC4PlopAndPaintDirector*>(pThis);
    if (!director) {
        return;
    }

    // Process deferred cancel for the stripper (before D3D, fires even if acquisition fails)
    if (director->propStripping_ && director->propStripperControl_) {
        director->propStripperControl_->ProcessPendingActions();
    }

    const bool needsOverlay =
        (director->propPainting_ && director->propPainterControl_) ||
        (director->propStripping_ && director->propStripperControl_);

    if (!director->imguiService_ || !needsOverlay) {
        return;
    }

    IDirect3DDevice7* device = nullptr;
    IDirectDraw7* dd = nullptr;
    if (!director->imguiService_->AcquireD3DInterfaces(&device, &dd)) {
        return;
    }

    if (director->propPainting_ && director->propPainterControl_) {
        director->propPainterControl_->DrawOverlay(device);
    }
    if (director->propStripping_ && director->propStripperControl_) {
        director->propStripperControl_->DrawOverlay(device);
    }
    device->Release();
    dd->Release();
}

void SC4PlopAndPaintDirector::SetLotPlopPanelVisible(const bool visible) {
    if (!imguiService_ || !panelRegistered_ || !panel_) {
        return;
    }

    panelVisible_ = visible;
    panel_->SetOpen(visible);
}

void SC4PlopAndPaintDirector::PostCityInit_(const cIGZMessage2Standard* pStandardMsg) {
    pCity_ = static_cast<cISC4City*>(pStandardMsg->GetVoid1());

    cISC4AppPtr pSC4App;
    if (pSC4App) {
        cIGZWin* pMainWindow = pSC4App->GetMainWindow();
        if (pMainWindow) {
            cIGZWin* pWinSC4App = pMainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
            if (pWinSC4App) {
                if (pWinSC4App->GetChildAs(
                    kGZWin_SC4View3DWin, kGZIID_cISC4View3DWin, reinterpret_cast<void**>(&pView3D_))) {
                    LOG_INFO("Acquired View3D interface");
                    RegisterLotPlopShortcut_();
                }
            }
        }
    }
}

void SC4PlopAndPaintDirector::PreCityShutdown_(cIGZMessage2Standard* pStandardMsg) {
    SetLotPlopPanelVisible(false);
    StopPropStripping();
    StopPropPainting();
    if (propStripperControl_) {
        propStripperControl_->SetCity(nullptr);
    }
    if (propPainterControl_) {
        propPainterControl_->SetCity(nullptr);
    }
    pCity_ = nullptr;
    if (pView3D_) {
        pView3D_->Release();
        pView3D_ = nullptr;
    }
    UnregisterLotPlopShortcut_();
    LOG_INFO("City shutdown - released resources");
}

void SC4PlopAndPaintDirector::ToggleLotPlopPanel_() {
    SetLotPlopPanelVisible(!panelVisible_);
}

bool SC4PlopAndPaintDirector::RegisterLotPlopShortcut_() {
    if (shortcutRegistered_) {
        return true;
    }
    if (!pView3D_) {
        LOG_WARN("Cannot register lot plop shortcut: View3D not available");
        return false;
    }
    if (!pMS2_) {
        LOG_WARN("Cannot register lot plop shortcut: message server not available");
        return false;
    }

    cIGZPersistResourceManagerPtr pRM;
    if (!pRM) {
        LOG_WARN("Cannot register lot plop shortcut: resource manager unavailable");
        return false;
    }

    cRZAutoRefCount<cIGZWinKeyAcceleratorRes> acceleratorRes;
    const cGZPersistResourceKey key(kKeyConfigType, kKeyConfigGroup, kKeyConfigInstance);
    if (!pRM->GetPrivateResource(key, kGZIID_cIGZWinKeyAcceleratorRes,
                                 acceleratorRes.AsPPVoid(), 0, nullptr)) {
        LOG_WARN("Failed to load key config resource 0x{:08X}/0x{:08X}/0x{:08X}",
                 kKeyConfigType, kKeyConfigGroup, kKeyConfigInstance);
        return false;
    }

    auto* accelerator = pView3D_->GetKeyAccelerator();
    if (!accelerator) {
        LOG_WARN("Cannot register lot plop shortcut: key accelerator not available");
        return false;
    }

    if (!acceleratorRes->RegisterResources(accelerator)) {
        LOG_WARN("Failed to register key accelerator resources");
        return false;
    }

    if (!pMS2_->AddNotification(this, kToggleLotPlopWindowShortcutID)) {
        LOG_WARN("Failed to register shortcut notification 0x{:08X}",
                 kToggleLotPlopWindowShortcutID);
        return false;
    }

    shortcutRegistered_ = true;
    return true;
}

void SC4PlopAndPaintDirector::UnregisterLotPlopShortcut_() {
    if (!shortcutRegistered_) {
        return;
    }

    if (pMS2_) {
        pMS2_->RemoveNotification(this, kToggleLotPlopWindowShortcutID);
    }
    shortcutRegistered_ = false;
}

std::filesystem::path SC4PlopAndPaintDirector::GetUserPluginsPath_() {
    try {
        const auto modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());
        return std::filesystem::path(modulePath.get()).parent_path();
    }
    catch (const wil::ResultException&) {
        return {};
    }
}
