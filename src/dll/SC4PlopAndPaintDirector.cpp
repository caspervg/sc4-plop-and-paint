// ReSharper disable CppDFAConstantConditions
// ReSharper disable CppDFAUnreachableCode
#include "SC4PlopAndPaintDirector.hpp"

#include <cIGZFrameWork.h>
#include <cstdio>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "cGZPersistResourceKey.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWinKeyAccelerator.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cRZBaseVariant.h"
#include "PlopAndPaintPanel.hpp"
#include "common/Constants.hpp"
#include "common/Utils.hpp"
#include "favorites/FavoritesRepository.hpp"
#include "flora/FloraRepository.hpp"
#include "lots/LotRepository.hpp"
#include "paint/RecentSwapPanel.hpp"
#include "props/PropPainterInputControl.hpp"
#include "props/PropRepository.hpp"
#include "props/PropStripperInputControl.hpp"
#include "public/cIGZS3DCameraService.h"
#include "public/S3DCameraServiceIds.h"
#include "public/cIGZS3DCameraService.h"
#include "terrain/TerrainDecalHook.hpp"
#include "utils/Logger.h"
#include "utils/Settings.h"

namespace {
    constexpr auto kSC4AdvancedLotPlopDirectorID = 0xE5C2B9A7u;

    constexpr auto kGZWin_WinSC4App = 0x6104489Au;
    constexpr auto kGZWin_SC4View3DWin = 0x9a47b417u;

    constexpr auto kLotPlopPanelId = 0xCA500001u;
    constexpr auto kStatusPanelId = 0xCA500002u;
    constexpr auto kRecentSwapPanelId = 0xCA500003u;
    constexpr auto kToggleLotPlopWindowShortcutID = 0x9F21C3A1u;
    constexpr auto kKeyConfigType = 0xA2E3D533u;
    constexpr auto kKeyConfigGroup = 0x8F1E6D69u;
    constexpr auto kKeyConfigInstance = 0x5CBCFBF8u;

    bool IsPaletteSource(const std::optional<RecentPaintSource>& source, const RecentPaintEntry::Kind kind) {
        if (!source.has_value()) {
            return false;
        }

        switch (source->sourceKind) {
        case RecentPaintEntry::SourceKind::PropAutoFamily:
        case RecentPaintEntry::SourceKind::PropUserFamily:
        case RecentPaintEntry::SourceKind::FloraFamily:
        case RecentPaintEntry::SourceKind::FloraChain:
            return true;
        case RecentPaintEntry::SourceKind::SingleProp:
        case RecentPaintEntry::SourceKind::SingleFlora:
            return false;
        default:
            return kind == RecentPaintEntry::Kind::Prop ? false : false;
        }
    }

    PropPaintSettings NormalizePaintSettings(const PropPaintSettings& settings,
                                             const std::optional<RecentPaintSource>& source,
                                             const RecentPaintEntry::Kind kind) {
        PropPaintSettings normalized = settings;
        if (!IsPaletteSource(source, kind)) {
            normalized.activePalette.clear();
            normalized.randomSeed = 0;
        }
        return normalized;
    }

    const char* PaintSwitchPolicyToString(const PaintSwitchPolicy policy) {
        switch (policy) {
        case PaintSwitchPolicy::Discard:
            return "discard";
        case PaintSwitchPolicy::Commit:
            return "commit";
        case PaintSwitchPolicy::KeepPending:
            return "keep";
        }

        return "unknown";
    }

    std::string ResolveRecentPaintName(const RecentPaintEntry::Kind kind,
                                       const uint32_t typeId,
                                       const std::string& requestedName,
                                       const PropRepository* const propRepository,
                                       const FloraRepository* const floraRepository) {
        if (!requestedName.empty()) {
            return requestedName;
        }

        if (kind == RecentPaintEntry::Kind::Prop) {
            if (const Prop* prop = propRepository ? propRepository->FindPropByInstanceId(typeId) : nullptr) {
                if (!prop->visibleName.empty()) {
                    return prop->visibleName;
                }
                if (!prop->exemplarName.empty()) {
                    return prop->exemplarName;
                }
            }
            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "Prop 0x%08X", typeId);
            return buffer;
        }

        if (const Flora* flora = floraRepository ? floraRepository->FindFloraByInstanceId(typeId) : nullptr) {
            if (!flora->visibleName.empty()) {
                return flora->visibleName;
            }
            if (!flora->exemplarName.empty()) {
                return flora->exemplarName;
            }
        }
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "Flora 0x%08X", typeId);
        return buffer;
    }
}

SC4PlopAndPaintDirector::SC4PlopAndPaintDirector() :
    imguiService_(nullptr), drawService_(nullptr), pView3D_(nullptr), panelRegistered_(false) {}

SC4PlopAndPaintDirector::~SC4PlopAndPaintDirector() = default;

uint32_t SC4PlopAndPaintDirector::GetDirectorID() const { return kSC4AdvancedLotPlopDirectorID; }

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
    UI::SetIconSize(settings.GetThumbnailDisplaySize());
    enableRecentPaints_ = settings.GetEnableRecentPaints();
    paintSwitchPolicy_ = settings.GetPaintSwitchPolicy();
    recentPaints_.SetMaxEntries(settings.GetRecentPaintMaxItems());
    LOG_INFO("Recent paints: enabled={}, maxItems={}, paintSwitchPolicy={}",
             enableRecentPaints_,
             recentPaints_.MaxEntries(),
             PaintSwitchPolicyToString(paintSwitchPolicy_));
    const auto thumbnailBackgroundColor = settings.GetThumbnailBackgroundColor();
    thumbnailBackgroundColor_ = IM_COL32(
        thumbnailBackgroundColor[0],
        thumbnailBackgroundColor[1],
        thumbnailBackgroundColor[2],
        thumbnailBackgroundColor[3]);
    const auto thumbnailBorderColor = settings.GetThumbnailBorderColor();
    thumbnailBorderColor_ = IM_COL32(
        thumbnailBorderColor[0],
        thumbnailBorderColor[1],
        thumbnailBorderColor[2],
        thumbnailBorderColor[3]);
    Logger::Shutdown();
    Logger::Initialize("SC4PlopAndPaint", logPath.string(), settings.GetLogToFile());
    Logger::SetLevel(settings.GetLogLevel());

    LOG_INFO("SC4AdvancedLotPlopDirector initialized");
    LOG_INFO("Using settings file: {}", settingsPath.string());
    LOG_INFO("Applied logging settings: level={}, file={}", spdlog::level::to_string_view(settings.GetLogLevel()),
             settings.GetLogToFile());

    terrainDecalHook_ = std::make_unique<TerrainDecal::TerrainDecalHook>(
        TerrainDecal::TerrainDecalHook::Options{
            .installEnabled = true,
            .enableExperimentalRenderer = true,
            .logInterceptedDraws = false,
        });
    if (terrainDecalHook_->Install()) {
        LOG_INFO("Terrain decal hook installed");
    }
    else {
        LOG_INFO("Terrain decal hook not installed: {}", terrainDecalHook_->GetLastError());
        terrainDecalHook_.reset();
    }

    cIGZMessageServer2Ptr pMS2;
    if (pMS2) {
        pMS2->AddNotification(this, kSC4MessagePostCityInit);
        pMS2->AddNotification(this, kSC4MessagePreCityShutdown);
        pMS2_ = pMS2;
        LOG_INFO("Registered for city messages");
    }

    if (mpFrameWork &&
        mpFrameWork->GetSystemService(kImGuiServiceID, GZIID_cIGZImGuiService,
                                      reinterpret_cast<void**>(&imguiService_))) {
        LOG_INFO("Acquired ImGui service");

        if (mpFrameWork->GetSystemService(kS3DCameraServiceID, GZIID_cIGZS3DCameraService,
                                          reinterpret_cast<void**>(&cameraService_))) {
            LOG_INFO("Acquired S3D camera service");
        }
        else {
            LOG_WARN("S3D camera service not available");
        }

        drawOverlayEnabled_ = settings.GetEnableDrawOverlay();
        if (!drawOverlayEnabled_) {
            LOG_INFO("Draw overlay disabled in settings");
        }

        if (mpFrameWork->GetSystemService(kDrawServiceID, GZIID_cIGZDrawService,
                                          reinterpret_cast<void**>(&drawService_))) {
            LOG_INFO("Acquired draw service");
            if (!drawService_->RegisterDrawPassCallback(DrawServicePass::PreDynamic, &DrawOverlayCallback_, this,
                                                        &drawCallbackToken_)) {
                LOG_WARN("Failed to register draw pass callback");
            }
        }
        else {
            LOG_WARN("Draw service not available");
        }

        lotRepository_ = std::make_unique<LotRepository>();
        propRepository_ = std::make_unique<PropRepository>();
        floraRepository_ = std::make_unique<FloraRepository>();
        favoritesRepository_ = std::make_unique<FavoritesRepository>(*propRepository_);

        lotRepository_->Load();
        propRepository_->Load();
        floraRepository_->Load();
        favoritesRepository_->Load();

        panel_ = std::make_unique<PlopAndPaintPanel>(this, lotRepository_.get(), propRepository_.get(),
                                                     floraRepository_.get(), favoritesRepository_.get(), imguiService_);

        const ImGuiPanelDesc desc =
            ImGuiPanelAdapter<PlopAndPaintPanel>::MakeDesc(panel_.get(), kLotPlopPanelId, 100, true);

        if (imguiService_->RegisterPanel(desc)) {
            panelRegistered_ = true;
            panelVisible_ = false;
            panel_->SetOpen(false);
            LOG_INFO("Registered ImGui panel");
        }

        statusPanel_ = std::make_unique<PaintStatusPanel>();
        const ImGuiPanelDesc statusDesc =
            ImGuiPanelAdapter<PaintStatusPanel>::MakeDesc(statusPanel_.get(), kStatusPanelId, 101, true);
        if (imguiService_->RegisterPanel(statusDesc)) {
            statusPanelRegistered_ = true;
            LOG_INFO("Registered status panel");
        }

        if (enableRecentPaints_) {
            swapPanel_ = std::make_unique<RecentSwapPanel>();
            swapPanel_->SetDirector(this);
            swapPanel_->SetRepositories(propRepository_.get(), floraRepository_.get(), imguiService_);
            const ImGuiPanelDesc swapDesc = ImGuiPanelAdapter<RecentSwapPanel>::MakeDesc(
                swapPanel_.get(), kRecentSwapPanelId, 102, true
            );
            if (imguiService_->RegisterPanel(swapDesc)) {
                swapPanelRegistered_ = true;
                LOG_INFO("Registered recent swap panel");
            }
        }

        if (enableRecentPaints_) {
            recentPaints_.Deserialize(favoritesRepository_->GetRecentPaintsData());
            recentPaints_.Validate(
                [this](const uint32_t id) { return propRepository_->FindPropByInstanceId(id) != nullptr; },
                [this](const uint32_t id) { return floraRepository_->FindFloraByInstanceId(id) != nullptr; },
                [this](const uint32_t id) {
                    if (const auto* prop = propRepository_->FindPropByInstanceId(id)) {
                        return MakeGIKey(prop->groupId.value(), prop->instanceId.value());
                    }
                    return 0ULL;
                },
                [this](const uint32_t id) {
                    if (const auto* flora = floraRepository_->FindFloraByInstanceId(id)) {
                        return MakeGIKey(flora->groupId.value(), flora->instanceId.value());
                    }
                    return 0ULL;
                });
            SyncRecentPaintsCache_();
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
    PersistRecentPaints_();

    if (terrainDecalHook_) {
        terrainDecalHook_->Uninstall();
        terrainDecalHook_.reset();
    }

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

    if (floraStripperControl_) {
        StopFloraStripping();
        floraStripperControl_->SetCity(nullptr);
        floraStripperControl_->Shutdown();
        floraStripperControl_.Reset();
    }

    if (propPainterControl_) {
        StopPropPainting();
        propPainterControl_->SetCity(nullptr);
        propPainterControl_->Shutdown();
        propPainterControl_.Reset();
    }

    if (floraPlacerControl_) {
        StopFloraPainting();
        floraPlacerControl_->SetCity(nullptr);
        floraPlacerControl_->Shutdown();
        floraPlacerControl_.Reset();
    }

    if (imguiService_ && statusPanelRegistered_) {
        imguiService_->UnregisterPanel(kStatusPanelId);
        statusPanelRegistered_ = false;
    }
    statusPanel_.reset();

    if (imguiService_ && swapPanelRegistered_) {
        imguiService_->UnregisterPanel(kRecentSwapPanelId);
        swapPanelRegistered_ = false;
    }
    swapPanel_.reset();

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
    floraRepository_.reset();
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
    case kSC4MessagePostCityInit:
        PostCityInit_(pStandardMsg);
        break;
    case kSC4MessagePreCityShutdown:
        PreCityShutdown_(pStandardMsg);
        break;
    case kToggleLotPlopWindowShortcutID:
        ToggleLotPlopPanel_();
        break;
    default:
        break;
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

    if (!pCmdServer->CreateCommandParameterSet(&pCmd1) || !pCmd1 || !pCmdServer->CreateCommandParameterSet(&pCmd2) ||
        !pCmd2) {
        if (pCmd1)
            pCmd1->Release();
        if (pCmd2)
            pCmd2->Release();
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
                                                const std::string& name,
                                                const std::optional<RecentPaintSource>& source) {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start prop painting: city or view not available");
        return false;
    }

    if (!PrepareForExclusiveActivation_(true, false, false)) {
        LOG_INFO("Blocked prop paint switch while another tool still has a sketch in progress");
        return false;
    }

    const PropPaintSettings normalizedSettings =
        NormalizePaintSettings(settings, source, RecentPaintEntry::Kind::Prop);

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

    const bool reusingActivePropControl =
        propPainting_ && propPainterControl_ &&
        pView3D_ && pView3D_->GetCurrentViewInputControl() == propPainterControl_;

    if (reusingActivePropControl) {
        if (propPainterControl_->HasPendingSketch()) {
            LOG_INFO("Blocked prop paint switch while the current sketch is still in progress");
            return false;
        }
        ApplySwitchPolicy_(propPainterControl_);
    }

    propPainterControl_->SetOnQuickSwap(enableRecentPaints_ ? std::function<void(size_t)>([this](const size_t index) {
        ActivateRecentPaint(index);
    }) : std::function<void(size_t)>());
    propPainterControl_->SetCity(pCity_);
    propPainterControl_->SetWindow(pView3D_->AsIGZWin());
    propPainterControl_->SetCameraService(cameraService_);
    propPainterControl_->SetPropRepository(propRepository_.get());
    propPainterControl_->SetOnCancel([this]() {
        if (pView3D_ && propPainterControl_ && pView3D_->GetCurrentViewInputControl() == propPainterControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        propPainting_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped prop painting");
    });

    propPainterControl_->SetPropToPaint(propId, normalizedSettings, name);
    if (!reusingActivePropControl) {
        if (!pView3D_->SetCurrentViewInputControl(
            propPainterControl_,
            cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
            LOG_WARN("Failed to set prop painter as current view input control");
            UpdatePaintPanels_();
            return false;
        }
    }

    propPainting_ = true;
    if (enableRecentPaints_) {
        recentPaints_.Push(BuildRecentPaintEntry_(RecentPaintEntry::Kind::Prop, propId, normalizedSettings, name, source));
        SyncRecentPaintsCache_();
    }
    UpdatePaintPanels_();
    LOG_INFO("Started prop painting: 0x{:08X}, rotation {}", propId, normalizedSettings.rotation);
    return true;
}

bool SC4PlopAndPaintDirector::SwitchPropPaintingTarget(uint32_t propId, const std::string& name,
                                                       const std::optional<RecentPaintSource>& source) {
    if (!propPainterControl_ || !propPainting_ || !pView3D_) {
        return false;
    }
    if (pView3D_->GetCurrentViewInputControl() != propPainterControl_) {
        return false;
    }

    PropPaintSettings settings = propPainterControl_->GetSettings();
    settings = NormalizePaintSettings(settings, source, RecentPaintEntry::Kind::Prop);
    return StartPropPainting(propId, settings, name, source);
}

void SC4PlopAndPaintDirector::StopPropPainting() {
    if (propPainterControl_) {
        propPainterControl_->CancelAllPlacements();
    }

    if (pView3D_ && propPainterControl_) {
        if (pView3D_->GetCurrentViewInputControl() == propPainterControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }

    propPainting_ = false;
    UpdatePaintPanels_();
    LOG_INFO("Stopped prop painting");
}

bool SC4PlopAndPaintDirector::IsPropPainting() const { return propPainting_; }

bool SC4PlopAndPaintDirector::StartFloraPainting(const uint32_t floraTypeId,
                                                 const PropPaintSettings& settings,
                                                 const std::string& name,
                                                 const std::optional<RecentPaintSource>& source) {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start flora painting: city or view not available");
        return false;
    }

    if (!PrepareForExclusiveActivation_(false, true, false, false)) {
        LOG_INFO("Blocked flora paint switch while another tool still has a sketch in progress");
        return false;
    }

    const PropPaintSettings normalizedSettings =
        NormalizePaintSettings(settings, source, RecentPaintEntry::Kind::Flora);

    if (!floraPlacerControl_) {
        auto* control = new FloraPainterInputControl();
        floraPlacerControl_ = control;
        if (!floraPlacerControl_->Init()) {
            LOG_ERROR("Failed to initialize FloraPlacerInputControl");
            floraPlacerControl_.Reset();
            return false;
        }
    }

    const bool reusingActiveFloraControl =
        floraPainting_ && floraPlacerControl_ &&
        pView3D_ && pView3D_->GetCurrentViewInputControl() == floraPlacerControl_;

    if (reusingActiveFloraControl) {
        if (floraPlacerControl_->HasPendingSketch()) {
            LOG_INFO("Blocked flora paint switch while the current sketch is still in progress");
            return false;
        }
        ApplySwitchPolicy_(floraPlacerControl_);
    }

    floraPlacerControl_->SetOnQuickSwap(enableRecentPaints_ ? std::function<void(size_t)>([this](const size_t index) {
        ActivateRecentPaint(index);
    }) : std::function<void(size_t)>());
    floraPlacerControl_->SetCity(pCity_);
    floraPlacerControl_->SetWindow(pView3D_->AsIGZWin());
    floraPlacerControl_->SetCameraService(cameraService_);
    floraPlacerControl_->SetFloraRepository(floraRepository_.get());
    floraPlacerControl_->SetOnCancel([this]() {
        if (pView3D_ && floraPlacerControl_ &&
            pView3D_->GetCurrentViewInputControl() == floraPlacerControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        floraPainting_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped flora painting");
    });

    floraPlacerControl_->SetFloraToPaint(floraTypeId, normalizedSettings, name);
    if (!reusingActiveFloraControl) {
        if (!pView3D_->SetCurrentViewInputControl(
            floraPlacerControl_,
            cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
            LOG_WARN("Failed to set flora placer as current view input control");
            UpdatePaintPanels_();
            return false;
        }
    }

    floraPainting_ = true;
    if (enableRecentPaints_) {
        recentPaints_.Push(BuildRecentPaintEntry_(RecentPaintEntry::Kind::Flora, floraTypeId, normalizedSettings, name, source));
        SyncRecentPaintsCache_();
    }
    UpdatePaintPanels_();
    LOG_INFO("Started flora painting: 0x{:08X}", floraTypeId);
    return true;
}

void SC4PlopAndPaintDirector::StopFloraPainting() {
    if (floraPlacerControl_) {
        floraPlacerControl_->CancelAllPlacements();
    }

    if (pView3D_ && floraPlacerControl_) {
        if (pView3D_->GetCurrentViewInputControl() == floraPlacerControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }

    floraPainting_ = false;
    UpdatePaintPanels_();
    LOG_INFO("Stopped flora painting");
}

bool SC4PlopAndPaintDirector::IsFloraPainting() const {
    return floraPainting_;
}

bool SC4PlopAndPaintDirector::StartFloraStripping() {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start flora stripping: city or view not available");
        return false;
    }

    if (!PrepareForExclusiveActivation_(false, false, false, true)) {
        LOG_INFO("Blocked flora stripping while another tool still has a sketch in progress");
        return false;
    }

    if (!floraStripperControl_) {
        auto* control = new FloraStripperInputControl();
        floraStripperControl_ = control;
        if (!floraStripperControl_->Init()) {
            LOG_ERROR("Failed to initialize FloraStripperInputControl");
            floraStripperControl_.Reset();
            return false;
        }
    }

    floraStripperControl_->SetCity(pCity_);
    floraStripperControl_->SetWindow(pView3D_->AsIGZWin());
    floraStripperControl_->SetOnCancel([this]() {
        if (pView3D_ && floraStripperControl_ &&
            pView3D_->GetCurrentViewInputControl() == floraStripperControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        floraStripping_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped flora stripping");
    });

    if (!pView3D_->SetCurrentViewInputControl(
        floraStripperControl_,
        cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        LOG_WARN("Failed to set flora stripper as current view input control");
        UpdatePaintPanels_();
        return false;
    }

    floraStripping_ = true;
    LOG_INFO("Started flora stripping");
    return true;
}

void SC4PlopAndPaintDirector::StopFloraStripping() {
    if (pView3D_ && floraStripperControl_) {
        if (pView3D_->GetCurrentViewInputControl() == floraStripperControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }
    floraStripping_ = false;
    UpdatePaintPanels_();
    LOG_INFO("Stopped flora stripping");
}

bool SC4PlopAndPaintDirector::IsFloraStripping() const {
    return floraStripping_;
}

bool SC4PlopAndPaintDirector::StartPropStripping() {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start prop stripping: city or view not available");
        return false;
    }

    if (!PrepareForExclusiveActivation_(false, false, true, false)) {
        LOG_INFO("Blocked prop stripping while another tool still has a sketch in progress");
        return false;
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

    if (propStripperControl_->GetEnabledSources() == PropStripperInputControl::SourceFlagNone) {
        LOG_WARN("Cannot start prop stripping: no prop sources enabled!");
        return false;
    }

    propStripperControl_->SetCity(pCity_);
    propStripperControl_->SetWindow(pView3D_->AsIGZWin());
    propStripperControl_->SetOnCancel([this]() {
        if (pView3D_ && propStripperControl_ &&
            pView3D_->GetCurrentViewInputControl() == propStripperControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        propStripping_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped prop stripping");
    });

    if (!pView3D_->SetCurrentViewInputControl(
        propStripperControl_,
        cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        LOG_WARN("Failed to set prop stripper as current view input control");
        UpdatePaintPanels_();
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
    UpdatePaintPanels_();
    LOG_INFO("Stopped prop stripping");
}

bool SC4PlopAndPaintDirector::IsPropStripping() const {
    return propStripping_;
}

void SC4PlopAndPaintDirector::SetPropStripperSources(const uint32_t sourceFlags) {
    if (!propStripperControl_) {
        auto* control = new PropStripperInputControl();
        propStripperControl_ = control;
        if (!propStripperControl_ || !propStripperControl_->Init()) {
            LOG_ERROR("Failed to initialize PropStripperInputControl");
            propStripperControl_.Reset();
            return;
        }
    }

    propStripperControl_->SetEnabledSources(sourceFlags);
}

uint32_t SC4PlopAndPaintDirector::GetPropStripperSources() const {
    if (!propStripperControl_) {
        return PropStripperInputControl::SourceFlagCity;
    }

    return propStripperControl_->GetEnabledSources();
}

BasePainterInputControl* SC4PlopAndPaintDirector::GetActivePainterControl() const {
    if (!pView3D_) {
        return nullptr;
    }

    auto* currentControl = pView3D_->GetCurrentViewInputControl();
    if (propPainterControl_ && currentControl == propPainterControl_) {
        return propPainterControl_;
    }
    if (floraPlacerControl_ && currentControl == floraPlacerControl_) {
        return floraPlacerControl_;
    }

    return nullptr;
}

const RecentPaintHistory& SC4PlopAndPaintDirector::GetRecentPaintHistory() const {
    return recentPaints_;
}

bool SC4PlopAndPaintDirector::ActivateRecentPaint(const size_t index) {
    if (!enableRecentPaints_) {
        return false;
    }

    BasePainterInputControl* activeControl = GetActivePainterControl();
    if (!activeControl || activeControl->HasPendingCancel() || activeControl->HasPendingSketch()) {
        return false;
    }
    if (index == 0 || index >= recentPaints_.Size()) {
        return false;
    }

    const RecentPaintEntry entry = recentPaints_.Entries()[index];
    PropPaintSettings settings = activeControl->GetSettings();
    settings.activePalette = entry.palette;
    settings.randomSeed = 0;

    const RecentPaintSource source{
        .sourceKind = entry.sourceKind,
        .sourceId = entry.sourceId
    };

    if (entry.kind == RecentPaintEntry::Kind::Prop) {
        if (entry.palette.empty() &&
            propPainterControl_ &&
            pView3D_ &&
            pView3D_->GetCurrentViewInputControl() == propPainterControl_) {
            return SwitchPropPaintingTarget(entry.typeId, entry.name, source);
        }
        return StartPropPainting(entry.typeId, settings, entry.name, source);
    }

    return StartFloraPainting(entry.typeId, settings, entry.name, source);
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

PreviewMode SC4PlopAndPaintDirector::GetDefaultPropPreviewMode() const noexcept {
    return defaultPropPreviewMode_;
}

ImU32 SC4PlopAndPaintDirector::GetThumbnailBackgroundColor() const noexcept {
    return thumbnailBackgroundColor_;
}

ImU32 SC4PlopAndPaintDirector::GetThumbnailBorderColor() const noexcept {
    return thumbnailBorderColor_;
}

void SC4PlopAndPaintDirector::ProcessPendingToolActions_() {
    if (propStripperControl_) {
        propStripperControl_->ProcessPendingActions();
    }
    if (floraStripperControl_) {
        floraStripperControl_->ProcessPendingActions();
    }
    if (propPainterControl_) {
        propPainterControl_->ProcessPendingActions();
    }
    if (floraPlacerControl_) {
        floraPlacerControl_->ProcessPendingActions();
    }
}

void SC4PlopAndPaintDirector::DrawOverlayCallback_(const DrawServicePass pass, const bool begin, void* pThis) {
    if (pass != DrawServicePass::PreDynamic || begin) {
        return;
    }

    auto* director = static_cast<SC4PlopAndPaintDirector*>(pThis);
    if (!director) {
        return;
    }

    director->ProcessPendingToolActions_();

    // Capture control pointers to avoid race conditions with flag changes
    PropPainterInputControl* painterControl = director->propPainterControl_;
    PropStripperInputControl* stripperControl = director->propStripperControl_;
    FloraPainterInputControl* floraControl = director->floraPlacerControl_;
    FloraStripperInputControl* floraStripperControl = director->floraStripperControl_;

    const bool needsOverlay = painterControl || stripperControl || floraControl || floraStripperControl;

    if (!director->drawOverlayEnabled_ || !director->imguiService_ || !needsOverlay) {
        return;
    }

    IDirect3DDevice7* device = nullptr;
    IDirectDraw7* dd = nullptr;
    if (!director->imguiService_->AcquireD3DInterfaces(&device, &dd)) {
        return;
    }

    if (painterControl) {
        painterControl->DrawOverlay(device);
    }
    if (stripperControl) {
        stripperControl->DrawOverlay(device);
    }
    if (floraControl) {
        floraControl->DrawOverlay(device);
    }
    if (floraStripperControl) {
        floraStripperControl->DrawOverlay(device);
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

void SC4PlopAndPaintDirector::UpdatePaintPanels_() {
    BasePainterInputControl* activeControl = GetActivePainterControl();

    if (statusPanel_) {
        if (activeControl) {
            statusPanel_->SetActiveControl(activeControl);
            statusPanel_->SetVisible(true);
        }
        else {
            statusPanel_->SetVisible(false);
        }
    }

    if (swapPanel_ && enableRecentPaints_) {
        swapPanel_->SetVisible(activeControl != nullptr && !recentPaints_.Empty());
    }
    else if (swapPanel_) {
        swapPanel_->SetVisible(false);
    }
}

void SC4PlopAndPaintDirector::SyncRecentPaintsCache_() {
    if (enableRecentPaints_ && favoritesRepository_) {
        favoritesRepository_->SetRecentPaintsData(recentPaints_.Serialize());
    }
    UpdatePaintPanels_();
}

void SC4PlopAndPaintDirector::PersistRecentPaints_() {
    if (!enableRecentPaints_ || !favoritesRepository_) {
        return;
    }

    favoritesRepository_->SetRecentPaintsData(recentPaints_.Serialize());
    favoritesRepository_->Save();
}

bool SC4PlopAndPaintDirector::CanPrepareForPaintSwitch_(BasePainterInputControl* control,
                                                        const bool isPaintingFlag) const {
    if (!control || !isPaintingFlag) {
        return true;
    }

    return !control->HasPendingSketch();
}

bool SC4PlopAndPaintDirector::PrepareForPaintSwitch_(BasePainterInputControl* control, bool& isPaintingFlag) {
    if (!control || !isPaintingFlag) {
        return true;
    }

    if (!CanPrepareForPaintSwitch_(control, isPaintingFlag)) {
        return false;
    }

    ApplySwitchPolicy_(control);

    if (pView3D_ && pView3D_->GetCurrentViewInputControl() == control) {
        pView3D_->RemoveCurrentViewInputControl(false);
    }

    isPaintingFlag = false;
    return true;
}

bool SC4PlopAndPaintDirector::PrepareForExclusiveActivation_(const bool keepPropPainting,
                                                             const bool keepFloraPainting,
                                                             const bool keepPropStripping,
                                                             const bool keepFloraStripping) {
    if (!keepPropPainting && !CanPrepareForPaintSwitch_(propPainterControl_, propPainting_)) {
        return false;
    }

    if (!keepFloraPainting && !CanPrepareForPaintSwitch_(floraPlacerControl_, floraPainting_)) {
        return false;
    }

    if (!keepPropPainting) {
        PrepareForPaintSwitch_(propPainterControl_, propPainting_);
    }

    if (!keepFloraPainting) {
        PrepareForPaintSwitch_(floraPlacerControl_, floraPainting_);
    }

    if (!keepPropStripping && propStripping_) {
        StopPropStripping();
    }

    if (!keepFloraStripping && floraStripping_) {
        StopFloraStripping();
    }

    return true;
}

void SC4PlopAndPaintDirector::ApplySwitchPolicy_(BasePainterInputControl* control) {
    if (!control) {
        return;
    }

    switch (paintSwitchPolicy_) {
    case PaintSwitchPolicy::Discard:
        control->CancelAllPlacements();
        break;
    case PaintSwitchPolicy::Commit:
        if (control->HasPendingPlacements()) {
            control->CommitPlacements();
        }
        break;
    case PaintSwitchPolicy::KeepPending:
        break;
    }
}

RecentPaintEntry SC4PlopAndPaintDirector::BuildRecentPaintEntry_(
    const RecentPaintEntry::Kind kind,
    const uint32_t typeId,
    const PropPaintSettings& settings,
    const std::string& name,
    const std::optional<RecentPaintSource>& source) const {
    RecentPaintEntry entry;
    entry.kind = kind;
    entry.typeId = typeId;
    entry.name = ResolveRecentPaintName(kind, typeId, name, propRepository_.get(), floraRepository_.get());
    entry.palette = settings.activePalette;

    if (source.has_value()) {
        entry.sourceKind = source->sourceKind;
        entry.sourceId = source->sourceId;
    }
    else if (kind == RecentPaintEntry::Kind::Prop) {
        entry.sourceKind = RecentPaintEntry::SourceKind::SingleProp;
        entry.sourceId = typeId;
    }
    else {
        entry.sourceKind = RecentPaintEntry::SourceKind::SingleFlora;
        entry.sourceId = typeId;
    }

    if (kind == RecentPaintEntry::Kind::Prop) {
        if (const Prop* prop = propRepository_ ? propRepository_->FindPropByInstanceId(typeId) : nullptr) {
            entry.thumbnailKey = MakeGIKey(prop->groupId.value(), prop->instanceId.value());
        }
    }
    else if (const Flora* flora = floraRepository_ ? floraRepository_->FindFloraByInstanceId(typeId) : nullptr) {
        entry.thumbnailKey = MakeGIKey(flora->groupId.value(), flora->instanceId.value());
    }

    return entry;
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
    PersistRecentPaints_();
    SetLotPlopPanelVisible(false);
    StopFloraStripping();
    StopPropStripping();
    StopPropPainting();
    StopFloraPainting();
    if (swapPanel_) {
        swapPanel_->SetVisible(false);
    }
    if (propStripperControl_) {
        propStripperControl_->SetCity(nullptr);
    }
    if (propPainterControl_) {
        propPainterControl_->SetCity(nullptr);
    }
    if (floraPlacerControl_) {
        floraPlacerControl_->SetCity(nullptr);
    }
    if (floraStripperControl_) {
        floraStripperControl_->SetCity(nullptr);
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
