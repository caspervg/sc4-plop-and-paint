// ReSharper disable CppDFAConstantConditions
// ReSharper disable CppDFAUnreachableCode
#include "SC4AdvancedLotPlopDirector.hpp"

#include <cIGZFrameWork.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include <chrono>
#include "cGZPersistResourceKey.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWinKeyAccelerator.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cRZBaseVariant.h"
#include "LotPlopPanel.hpp"
#include "PropPainterInputControl.hpp"
#include "public/cIGZS3DCameraService.h"
#include "public/S3DCameraServiceIds.h"
#include "rfl/cbor/load.hpp"
#include "rfl/cbor/save.hpp"
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
      , pView3D_(nullptr)
      , panelRegistered_(false) {
    spdlog::info("SC4AdvancedLotPlopDirector initialized");
}

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

        LoadLots_();
        LoadProps_();
        LoadFavorites_();

        panel_ = std::make_unique<LotPlopPanel>(this, imguiService_);
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
    if (imguiService_ && panelRegistered_) {
        SetLotPlopPanelVisible(false);
        imguiService_->UnregisterPanel(kLotPlopPanelId);
        panelRegistered_ = false;
        panel_.reset();
    }

    if (imguiService_) {
        imguiService_->Release();
        imguiService_ = nullptr;
    }
    if (propPainterControl_) {
        propPainterControl_->Shutdown();
        propPainterControl_.Reset();
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

const std::vector<Building>& SC4AdvancedLotPlopDirector::GetBuildings() const {
    return buildings_;
}

const std::vector<Prop>& SC4AdvancedLotPlopDirector::GetProps() const {
    return props_;
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

bool SC4AdvancedLotPlopDirector::IsFavorite(uint32_t lotInstanceId) const {
    return favoriteLotIds_.contains(lotInstanceId);
}

const std::unordered_set<uint32_t>& SC4AdvancedLotPlopDirector::GetFavoriteLotIds() const {
    return favoriteLotIds_;
}

void SC4AdvancedLotPlopDirector::ToggleFavorite(uint32_t lotInstanceId) {
    if (favoriteLotIds_.contains(lotInstanceId)) {
        favoriteLotIds_.erase(lotInstanceId);
        spdlog::info("Removed favorite: 0x{:08X}", lotInstanceId);
    }
    else {
        favoriteLotIds_.insert(lotInstanceId);
        spdlog::info("Added favorite: 0x{:08X}", lotInstanceId);
    }
    SaveFavorites_();
}

bool SC4AdvancedLotPlopDirector::IsPropFavorite(const uint32_t groupId, const uint32_t instanceId) const {
    return favoritePropIds_.contains(MakePropKey_(groupId, instanceId));
}

const std::unordered_set<uint64_t>& SC4AdvancedLotPlopDirector::GetFavoritePropIds() const {
    return favoritePropIds_;
}

void SC4AdvancedLotPlopDirector::TogglePropFavorite(const uint32_t groupId, const uint32_t instanceId) {
    const uint64_t key = MakePropKey_(groupId, instanceId);
    if (favoritePropIds_.contains(key)) {
        favoritePropIds_.erase(key);
        spdlog::info("Removed prop favorite: 0x{:08X}/0x{:08X}", groupId, instanceId);
    }
    else {
        favoritePropIds_.insert(key);
        spdlog::info("Added prop favorite: 0x{:08X}/0x{:08X}", groupId, instanceId);
    }
    SaveFavorites_();
}

void SC4AdvancedLotPlopDirector::SetLotPlopPanelVisible(bool visible) {
    if (!imguiService_ || !panelRegistered_ || !panel_) {
        return;
    }

    panelVisible_ = visible;
    panel_->SetOpen(visible);
}

bool SC4AdvancedLotPlopDirector::StartPropPainting(uint32_t propId, const PropPaintSettings& settings,
                                                   const std::string& name) {
    if (!pCity_ || !pView3D_) {
        spdlog::warn("Cannot start prop painting: city or view not available");
        return false;
    }
    if (propPainting_ && pView3D_ && pView3D_->GetCurrentViewInputControl() == propPainterControl_) {
        return SwitchPropPaintingTarget(propId, name);
    }

    if (!propPainterControl_) {
        auto* control = new PropPainterInputControl();
        propPainterControl_ = control;
    }
    propPainterControl_->SetCity(pCity_);
    propPainterControl_->SetWindow(pView3D_->AsIGZWin());
    propPainterControl_->SetCameraService(cameraService_);
    propPainterControl_->SetOnCancel([this]() { StopPropPainting(); });
    if (!propPainterControl_->Init()) {
        spdlog::error("Failed to initialize PropPainterInputControl");
        propPainterControl_.Reset();
        return false;
    }

    propPainterControl_->SetPropToPaint(propId, settings, name);

    pView3D_->RemoveAllViewInputControls(false);
    pView3D_->SetCurrentViewInputControl(
        propPainterControl_,
        cISC4View3DWin::ViewInputControlStackOperation_None
    );

    propPainting_ = true;
    spdlog::info("Started prop painting: 0x{:08X}, rotation {}", propId, settings.rotation);
    return true;
}

void SC4AdvancedLotPlopDirector::StopPropPainting() {
    if (!pView3D_) {
        spdlog::debug("StopPropPainting called without View3D");
        return;
    }
    if (!propPainting_) {
        spdlog::debug("StopPropPainting called while not painting");
        return;
    }

    pView3D_->RemoveCurrentViewInputControl(false);
    propPainting_ = false;
    spdlog::info("Stopped prop painting");
}

bool SC4AdvancedLotPlopDirector::IsPropPainting() const {
    return propPainting_;
}

bool SC4AdvancedLotPlopDirector::SwitchPropPaintingTarget(uint32_t propId, const std::string& name) {
    if (!propPainterControl_ || !propPainting_ || !pView3D_) {
        return false;
    }
    if (pView3D_->GetCurrentViewInputControl() != propPainterControl_) {
        return false;
    }

    const auto& settings = propPainterControl_->GetSettings();
    propPainterControl_->SetPropToPaint(propId, settings, name);
    spdlog::info("Switched prop paint target: 0x{:08X}, rotation {}", propId, settings.rotation);
    return true;
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

void SC4AdvancedLotPlopDirector::LoadLots_() {
    try {
        const auto pluginsPath = GetUserPluginsPath_();
        const auto cborPath = pluginsPath / "lot_configs.cbor";

        if (!std::filesystem::exists(cborPath)) {
            spdlog::warn("Lot config CBOR file not found: {}", cborPath.string());
            return;
        }

        auto result = rfl::cbor::load<std::vector<Building>>(cborPath.string());
        if (result) {
            buildings_ = std::move(*result);

            size_t lotCount = 0;
            std::unordered_set<uint64_t> lotKeys;
            size_t duplicateLots = 0;
            for (const auto& b : buildings_) {
                for (const auto& lot : b.lots) {
                    ++lotCount;
                    const uint64_t key = (static_cast<uint64_t>(lot.groupId.value()) << 32) | lot.instanceId.value();
                    if (!lotKeys.insert(key).second) {
                        ++duplicateLots;
                        spdlog::warn("Duplicate lot in CBOR: group=0x{:08X}, instance=0x{:08X}", lot.groupId.value(),
                                     lot.instanceId.value());
                    }
                }
            }

            spdlog::info("Loaded {} buildings / {} lots from {}", buildings_.size(), lotCount, cborPath.string());
            if (duplicateLots > 0) {
                spdlog::warn("Detected {} duplicate lot IDs in CBOR", duplicateLots);
            }
        }
        else {
            spdlog::error("Failed to load lots from CBOR file: {}", result.error().what());
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error loading lots: {}", e.what());
    }
}

void SC4AdvancedLotPlopDirector::LoadProps_() {
    try {
        const auto pluginsPath = GetUserPluginsPath_();
        const auto cborPath = pluginsPath / "props.cbor";

        if (!std::filesystem::exists(cborPath)) {
            spdlog::warn("Prop CBOR file not found: {}", cborPath.string());
            return;
        }

        if (auto result = rfl::cbor::load<std::vector<Prop>>(cborPath.string())) {
            spdlog::info("Loaded {} props from {}", result->size(), cborPath.string());
            props_ = std::move(*result);
        }
        else {
            spdlog::error("Failed to load props from CBOR file: {}", result.error().what());
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error loading props: {}", e.what());
    }
}

void SC4AdvancedLotPlopDirector::LoadFavorites_() {
    try {
        const auto pluginsPath = GetUserPluginsPath_();
        const auto cborPath = pluginsPath / "favorites.cbor";

        if (!std::filesystem::exists(cborPath)) {
            spdlog::info("Favorites file not found, starting with empty favorites");
            return;
        }

        if (auto result = rfl::cbor::load<AllFavorites>(cborPath.string())) {
            // Extract lot favorites from the loaded data
            favoriteLotIds_.clear();
            for (const auto& hexId : result->lots.items) {
                favoriteLotIds_.insert(hexId.value());
            }
            favoritePropIds_.clear();
            if (result->props) {
                for (const auto& hexId : result->props->items) {
                    favoritePropIds_.insert(hexId.value());
                }
            }
            spdlog::info("Loaded {} favorite lots from {}", favoriteLotIds_.size(), cborPath.string());
        }
        else {
            spdlog::warn("Failed to load favorites from CBOR file: {}", result.error().what());
        }
    }
    catch (const std::exception& e) {
        spdlog::warn("Error loading favorites (will start empty): {}", e.what());
    }
}

void SC4AdvancedLotPlopDirector::SaveFavorites_() const {
    try {
        const auto pluginsPath = GetUserPluginsPath_();
        const auto cborPath = pluginsPath / "favorites.cbor";

        // Build the AllFavorites structure
        AllFavorites allFavorites;
        allFavorites.version = 1;

        // Convert favorites set to vector of Hex<uint32_t>
        for (uint32_t id : favoriteLotIds_) {
            allFavorites.lots.items.emplace_back(id);
        }

        // Set timestamp to current time using std::chrono
        const auto now = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        localtime_s(&tm_now, &time_t_now);
        allFavorites.lastModified = rfl::Timestamp<"%Y-%m-%dT%H:%M:%S">(tm_now);

        if (!favoritePropIds_.empty()) {
            TabFavorites propFavorites;
            propFavorites.items.reserve(favoritePropIds_.size());
            for (uint64_t id : favoritePropIds_) {
                propFavorites.items.emplace_back(id);
            }
            allFavorites.props = std::move(propFavorites);
        }
        else {
            allFavorites.props = std::nullopt;
        }
        allFavorites.flora = std::nullopt;

        // Save to CBOR file
        if (const auto saveResult = rfl::cbor::save(cborPath.string(), allFavorites)) {
            spdlog::info("Saved {} favorites to {}", favoriteLotIds_.size(), cborPath.string());
        }
        else {
            spdlog::error("Failed to save favorites: {}", saveResult.error().what());
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error saving favorites: {}", e.what());
    }
}

std::filesystem::path SC4AdvancedLotPlopDirector::GetUserPluginsPath_() {
    // Get the directory where this DLL is loaded from
    try {
        // Get the module path using WIL's safe wrapper
        const auto modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());

        // Convert to filesystem::path and get the parent directory
        std::filesystem::path dllDir = std::filesystem::path(modulePath.get()).parent_path();
        spdlog::info("DLL directory: {}", dllDir.string());
        return dllDir;
    }
    catch (const wil::ResultException& e) {
        spdlog::error("Failed to get DLL directory: {}", e.what());
        return {};
    }
}

uint64_t SC4AdvancedLotPlopDirector::MakePropKey_(uint32_t groupId, uint32_t instanceId) {
    return (static_cast<uint64_t>(groupId) << 32) | instanceId;
}
