// ReSharper disable CppDFAConstantConditions
// ReSharper disable CppDFAUnreachableCode
#include "SC4AdvancedLotPlopDirector.hpp"

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
#include "PropRepository.hpp"
#include "Utils.hpp"
#include "public/cIGZS3DCameraService.h"
#include "public/S3DCameraServiceIds.h"
#include "spdlog/spdlog.h"

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

SC4AdvancedLotPlopDirector::SC4AdvancedLotPlopDirector()
    : imguiService_(nullptr)
      , drawService_(nullptr)
      , pView3D_(nullptr)
      , panelRegistered_(false) {
    spdlog::info("SC4AdvancedLotPlopDirector initialized");
}

SC4AdvancedLotPlopDirector::~SC4AdvancedLotPlopDirector() = default;

uint32_t SC4AdvancedLotPlopDirector::GetDirectorID() const {
    return kSC4AdvancedLotPlopDirectorID;
}

bool SC4AdvancedLotPlopDirector::OnStart(cIGZCOM* pCOM) {
    cRZMessage2COMDirector::OnStart(pCOM);

    if (auto* framework = RZGetFrameWork()) {
        framework->AddHook(this);
    }
    return true;
}

bool SC4AdvancedLotPlopDirector::PreFrameWorkInit() { return true; }
bool SC4AdvancedLotPlopDirector::PreAppInit() { return true; }

bool SC4AdvancedLotPlopDirector::PostAppInit() {
    cIGZMessageServer2Ptr pMS2;
    if (pMS2) {
        pMS2->AddNotification(this, kSC4MessagePostCityInit);
        pMS2->AddNotification(this, kSC4MessagePreCityShutdown);
        pMS2_ = pMS2;
        spdlog::info("Registered for city messages");
    }

    if (mpFrameWork && mpFrameWork->GetSystemService(kImGuiServiceID, GZIID_cIGZImGuiService,
                                                     reinterpret_cast<void**>(&imguiService_))) {
        spdlog::info("Acquired ImGui service");

        if (mpFrameWork->GetSystemService(kS3DCameraServiceID, GZIID_cIGZS3DCameraService,
                                          reinterpret_cast<void**>(&cameraService_))) {
            spdlog::info("Acquired S3D camera service");
        }
        else {
            spdlog::warn("S3D camera service not available");
        }

        if (mpFrameWork->GetSystemService(kDrawServiceID, GZIID_cIGZDrawService,
                                          reinterpret_cast<void**>(&drawService_))) {
            spdlog::info("Acquired draw service");
            if (!drawService_->RegisterDrawPassCallback(
                DrawServicePass::PreDynamic,
                &DrawOverlayCallback_,
                this,
                &drawCallbackToken_)) {
                spdlog::warn("Failed to register draw pass callback");
            }
        }
        else {
            spdlog::warn("Draw service not available");
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
            spdlog::info("Registered ImGui panel");
        }
    }
    else {
        spdlog::warn("ImGui service not found or not available");
    }

    return true;
}

bool SC4AdvancedLotPlopDirector::PreAppShutdown() { return true; }

bool SC4AdvancedLotPlopDirector::PostAppShutdown() {
    // Destroy objects that may call ImGuiService first.
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

    return true;
}

bool SC4AdvancedLotPlopDirector::PostSystemServiceShutdown() { return true; }

bool SC4AdvancedLotPlopDirector::AbortiveQuit() { return true; }

bool SC4AdvancedLotPlopDirector::OnInstall() { return true; }

bool SC4AdvancedLotPlopDirector::DoMessage(cIGZMessage2* pMsg) {
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

void SC4AdvancedLotPlopDirector::TriggerLotPlop(uint32_t lotInstanceId) const {
    if (!pView3D_) {
        spdlog::warn("Cannot plop: View3D not available (city not loaded?)");
        return;
    }

    cIGZCommandServerPtr pCmdServer;
    if (!pCmdServer) {
        spdlog::warn("Cannot plop: Command server not available");
        return;
    }

    cIGZCommandParameterSet* pCmd1 = nullptr;
    cIGZCommandParameterSet* pCmd2 = nullptr;

    if (!pCmdServer->CreateCommandParameterSet(&pCmd1) || !pCmd1 ||
        !pCmdServer->CreateCommandParameterSet(&pCmd2) || !pCmd2) {
        if (pCmd1) pCmd1->Release();
        if (pCmd2) pCmd2->Release();
        spdlog::error("Failed to create command parameter sets");
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

    spdlog::info("Triggered lot plop for instance ID: 0x{:08X}", lotInstanceId);

    pCmd1->Release();
    pCmd2->Release();
}

bool SC4AdvancedLotPlopDirector::StartPropPainting(uint32_t propId, const PropPaintSettings& settings,
                                                   const std::string& name) {
    if (!pCity_ || !pView3D_) {
        spdlog::warn("Cannot start prop painting: city or view not available");
        return false;
    }

    if (!propPainterControl_) {
        auto* control = new PropPainterInputControl();
        propPainterControl_ = control;
        if (!propPainterControl_) {
            spdlog::error("Failed to allocate PropPainterInputControl");
            return false;
        }

        if (!propPainterControl_->Init()) {
            spdlog::error("Failed to initialize PropPainterInputControl");
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
        spdlog::info("Stopped prop painting");
    });

    propPainterControl_->SetPropToPaint(propId, settings, name);
    if (!pView3D_->SetCurrentViewInputControl(
        propPainterControl_,
        cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        spdlog::warn("Failed to set prop painter as current view input control");
        return false;
    }

    propPainting_ = true;
    spdlog::info("Started prop painting: 0x{:08X}, rotation {}", propId, settings.rotation);
    return true;
}

bool SC4AdvancedLotPlopDirector::SwitchPropPaintingTarget(uint32_t propId, const std::string& name) {
    if (!propPainterControl_ || !propPainting_ || !pView3D_) {
        return false;
    }
    if (pView3D_->GetCurrentViewInputControl() != propPainterControl_) {
        return false;
    }

    const auto& settings = propPainterControl_->GetSettings();
    return StartPropPainting(propId, settings, name);
}

void SC4AdvancedLotPlopDirector::StopPropPainting() {
    if (pView3D_ && propPainterControl_) {
        if (pView3D_->GetCurrentViewInputControl() == propPainterControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }

    propPainting_ = false;
    spdlog::info("Stopped prop painting");
}

bool SC4AdvancedLotPlopDirector::IsPropPainting() const {
    return propPainting_;
}

void SC4AdvancedLotPlopDirector::DrawOverlayCallback_(const DrawServicePass pass, const bool begin, void* pThis) {
    if (pass != DrawServicePass::PreDynamic || begin) {
        return;
    }

    auto* director = static_cast<SC4AdvancedLotPlopDirector*>(pThis);
    if (!director || !director->imguiService_ || !director->propPainting_ || !director->propPainterControl_) {
        return;
    }

    IDirect3DDevice7* device = nullptr;
    IDirectDraw7* dd = nullptr;
    if (!director->imguiService_->AcquireD3DInterfaces(&device, &dd)) {
        return;
    }

    director->propPainterControl_->DrawOverlay(device);
    device->Release();
    dd->Release();
}

void SC4AdvancedLotPlopDirector::SetLotPlopPanelVisible(const bool visible) {
    if (!imguiService_ || !panelRegistered_ || !panel_) {
        return;
    }

    panelVisible_ = visible;
    panel_->SetOpen(visible);
}

void SC4AdvancedLotPlopDirector::PostCityInit_(const cIGZMessage2Standard* pStandardMsg) {
    pCity_ = static_cast<cISC4City*>(pStandardMsg->GetVoid1());

    cISC4AppPtr pSC4App;
    if (pSC4App) {
        cIGZWin* pMainWindow = pSC4App->GetMainWindow();
        if (pMainWindow) {
            cIGZWin* pWinSC4App = pMainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
            if (pWinSC4App) {
                if (pWinSC4App->GetChildAs(
                    kGZWin_SC4View3DWin, kGZIID_cISC4View3DWin, reinterpret_cast<void**>(&pView3D_))) {
                    spdlog::info("Acquired View3D interface");
                    RegisterLotPlopShortcut_();
                }
            }
        }
    }
}

void SC4AdvancedLotPlopDirector::PreCityShutdown_(cIGZMessage2Standard* pStandardMsg) {
    SetLotPlopPanelVisible(false);
    StopPropPainting();
    if (propPainterControl_) {
        propPainterControl_->SetCity(nullptr);
    }
    pCity_ = nullptr;
    if (pView3D_) {
        pView3D_->Release();
        pView3D_ = nullptr;
    }
    UnregisterLotPlopShortcut_();
    spdlog::info("City shutdown - released resources");
}

void SC4AdvancedLotPlopDirector::ToggleLotPlopPanel_() {
    SetLotPlopPanelVisible(!panelVisible_);
}

bool SC4AdvancedLotPlopDirector::RegisterLotPlopShortcut_() {
    if (shortcutRegistered_) {
        return true;
    }
    if (!pView3D_) {
        spdlog::warn("Cannot register lot plop shortcut: View3D not available");
        return false;
    }
    if (!pMS2_) {
        spdlog::warn("Cannot register lot plop shortcut: message server not available");
        return false;
    }

    cIGZPersistResourceManagerPtr pRM;
    if (!pRM) {
        spdlog::warn("Cannot register lot plop shortcut: resource manager unavailable");
        return false;
    }

    cRZAutoRefCount<cIGZWinKeyAcceleratorRes> acceleratorRes;
    const cGZPersistResourceKey key(kKeyConfigType, kKeyConfigGroup, kKeyConfigInstance);
    if (!pRM->GetPrivateResource(key, kGZIID_cIGZWinKeyAcceleratorRes,
                                 acceleratorRes.AsPPVoid(), 0, nullptr)) {
        spdlog::warn("Failed to load key config resource 0x{:08X}/0x{:08X}/0x{:08X}",
                     kKeyConfigType, kKeyConfigGroup, kKeyConfigInstance);
        return false;
    }

    auto* accelerator = pView3D_->GetKeyAccelerator();
    if (!accelerator) {
        spdlog::warn("Cannot register lot plop shortcut: key accelerator not available");
        return false;
    }

    if (!acceleratorRes->RegisterResources(accelerator)) {
        spdlog::warn("Failed to register key accelerator resources");
        return false;
    }

    if (!pMS2_->AddNotification(this, kToggleLotPlopWindowShortcutID)) {
        spdlog::warn("Failed to register shortcut notification 0x{:08X}",
                     kToggleLotPlopWindowShortcutID);
        return false;
    }

    shortcutRegistered_ = true;
    return true;
}

void SC4AdvancedLotPlopDirector::UnregisterLotPlopShortcut_() {
    if (!shortcutRegistered_) {
        return;
    }

    if (pMS2_) {
        pMS2_->RemoveNotification(this, kToggleLotPlopWindowShortcutID);
    }
    shortcutRegistered_ = false;
}

std::filesystem::path SC4AdvancedLotPlopDirector::GetUserPluginsPath_() {
    try {
        const auto modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());
        std::filesystem::path dllDir = std::filesystem::path(modulePath.get()).parent_path();
        spdlog::info("DLL directory: {}", dllDir.string());
        return dllDir;
    }
    catch (const wil::ResultException& e) {
        spdlog::error("Failed to get DLL directory: {}", e.what());
        return {};
    }
}
