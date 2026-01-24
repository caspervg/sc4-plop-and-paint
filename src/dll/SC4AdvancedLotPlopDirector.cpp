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
#include "LotPlopPanel.hpp"
#include "rfl/cbor/load.hpp"
#include "rfl/cbor/save.hpp"
#include "spdlog/spdlog.h"
#include <chrono>

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

        LoadLots_();
        LoadFavorites_();

        auto* panel = new LotPlopPanel(this, imguiService_);
        const ImGuiPanelDesc desc = ImGuiPanelAdapter<LotPlopPanel>::MakeDesc(
            panel, kLotPlopPanelId, 100, false
        );

        if (imguiService_->RegisterPanel(desc)) {
            panelRegistered_ = true;
            panelVisible_ = false;
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
    }

    if (imguiService_) {
        imguiService_->Release();
        imguiService_ = nullptr;
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

void SC4AdvancedLotPlopDirector::ToggleLotPlopPanel_() {
    SetLotPlopPanelVisible(!panelVisible_);
}

void SC4AdvancedLotPlopDirector::SetLotPlopPanelVisible(bool visible) {
    if (!imguiService_ || !panelRegistered_) {
        return;
    }

    panelVisible_ = visible;
    imguiService_->SetPanelVisible(kLotPlopPanelId, visible);
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

const std::vector<Lot>& SC4AdvancedLotPlopDirector::GetLots() const {
    return lots_;
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
    } else {
        favoriteLotIds_.insert(lotInstanceId);
        spdlog::info("Added favorite: 0x{:08X}", lotInstanceId);
    }
    SaveFavorites_();
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
    pCity_ = nullptr;
    pView3D_ = nullptr;
    UnregisterLotPlopShortcut_();
    spdlog::info("City shutdown - released resources");
}

void SC4AdvancedLotPlopDirector::LoadLots_() {
    try {
        const auto pluginsPath = GetUserPluginsPath_();
        const auto cborPath = pluginsPath / "lot_configs.cbor";

        if (!std::filesystem::exists(cborPath)) {
            spdlog::warn("Lot config CBOR file not found: {}", cborPath.string());
            return;
        }

        auto result = rfl::cbor::load<std::vector<Lot>>(cborPath.string());
        if (result) {
            lots_ = std::move(*result);
            spdlog::info("Loaded {} lots from {}", lots_.size(), cborPath.string());
        }
        else {
            spdlog::error("Failed to load lots from CBOR file: {}", result.error().what());
        }
    }
    catch (const std::exception& e) {
        spdlog::error("Error loading lots: {}", e.what());
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
            spdlog::info("Loaded {} favorite lots from {}", favoriteLotIds_.size(), cborPath.string());
        } else {
            spdlog::warn("Failed to load favorites from CBOR file: {}", result.error().what());
        }
    } catch (const std::exception& e) {
        spdlog::warn("Error loading favorites (will start empty): {}", e.what());
    }
}

void SC4AdvancedLotPlopDirector::SaveFavorites_() {
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

        // Leave props and flora as nullopt for now
        allFavorites.props = std::nullopt;
        allFavorites.flora = std::nullopt;

        // Save to CBOR file
        if (const auto saveResult = rfl::cbor::save(cborPath.string(), allFavorites)) {
            spdlog::info("Saved {} favorites to {}", favoriteLotIds_.size(), cborPath.string());
        } else {
            spdlog::error("Failed to save favorites: {}", saveResult.error().what());
        }
    } catch (const std::exception& e) {
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
    } catch (const wil::ResultException& e) {
        spdlog::error("Failed to get DLL directory: {}", e.what());
        return {};
    }
}
