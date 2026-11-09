// ReSharper disable CppDFAConstantConditions
// ReSharper disable CppDFAUnreachableCode
#include "SC4PlopAndPaintDirector.hpp"

#include <cIGZFrameWork.h>
#include <utility>
#include <cstdio>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "cGZPersistResourceKey.h"
#include "cIGZCommandParameterSet.h"
#include "cIGZPersistResourceManager.h"
#include "cIGZWinKeyAccelerator.h"
#include "cIGZWinKeyAcceleratorRes.h"
#include "cISC4DBSegment.h"
#include "public/cIGZTerrainPaintService.h"
#include "cRZBaseVariant.h"
#include "PlopAndPaintPanel.hpp"
#include "common/Constants.hpp"
#include "common/Utils.hpp"
#include "favorites/FavoritesRepository.hpp"
#include "decals/DecalPainterInputControl.hpp"
#include "decals/DecalRepository.hpp"
#include "decals/DecalStripperInputControl.hpp"
#include "flora/FloraRepository.hpp"
#include "ground/GroundPaintRecord.hpp"
#include "lots/LotRepository.hpp"
#include "lots/BuildingStyleService.hpp"
#include "paint/RecentSwapPanel.hpp"
#include "pick/DecalPickStrategy.hpp"
#include "pick/FloraPickStrategy.hpp"
#include "pick/LotPickStrategy.hpp"
#include "pick/PropPickStrategy.hpp"
#include "props/PropPainterInputControl.hpp"
#include "props/PropRepository.hpp"
#include "props/PropStripperInputControl.hpp"
#include "utils/Logger.h"
#include "utils/Settings.h"
#include "utils/UpdateCheck.hpp"

namespace {
    constexpr auto kSC4AdvancedLotPlopDirectorID = 0xE5C2B9A7u;

    constexpr auto kGZWin_WinSC4App = 0x6104489Au;
    constexpr auto kGZWin_SC4View3DWin = 0x9a47b417u;

    constexpr auto kLotPlopPanelId = 0xCA500001u;
    constexpr auto kStatusPanelId = 0xCA500002u;
    constexpr auto kRecentSwapPanelId = 0xCA500003u;
    constexpr auto kScenePickStatusPanelId = 0xCA500004u;
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

SC4PlopAndPaintDirector::SC4PlopAndPaintDirector()
    : imguiService_(nullptr)
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
    LOG_INFO("Applied logging settings: level={}, file={}",
             spdlog::level::to_string_view(settings.GetLogLevel()), settings.GetLogToFile());

    if (settings.GetCheckForUpdates()) {
        UpdateCheck::Start("caspervg/sc4-plop-and-paint", "SC4PlopAndPaint", SC4_PLOP_AND_PAINT_VERSION);
    }

    cIGZMessageServer2Ptr pMS2;
    if (pMS2) {
        pMS2->AddNotification(this, kSC4MessagePostCityInit);
        pMS2->AddNotification(this, kSC4MessagePreCityShutdown);
        pMS2->AddNotification(this, kSC4MessagePostCityInitComplete);
        pMS2->AddNotification(this, kMessageBuildingStyleCheckboxChanged);
        pMS2->AddNotification(this, kSC4MessagePostRegionInit);
        pMS2->AddNotification(this, kSC4MessageLoad);
        pMS2->AddNotification(this, kSC4MessageSave);
        pMS2_ = pMS2;
        LOG_INFO("Registered for city messages");
    }

    if (mpFrameWork && mpFrameWork->GetSystemService(kImGuiServiceID, GZIID_cIGZImGuiService,
                                                     reinterpret_cast<void**>(&imguiService_))) {
        LOG_INFO("Acquired ImGui service");

        drawOverlayEnabled_ = settings.GetEnableDrawOverlay();
        if (!drawOverlayEnabled_) {
            LOG_INFO("Draw overlay disabled in settings");
        }

        if (mpFrameWork->GetSystemService(kWorldDrawServiceID, GZIID_cIGZWorldDrawService,
                                          reinterpret_cast<void**>(&worldDrawService_))) {
            LOG_INFO("Acquired world draw service");
        }
        else {
            LOG_WARN("World draw service not available, tool previews are not drawn in the city");
        }

        // Tools hand deferred work (cancels, picks, seasonal preview refreshes) to this tick,
        // outside their own input handlers.
        if (toolTickService_.Init() && mpFrameWork->AddSystemService(&toolTickService_) &&
            mpFrameWork->AddToTick(&toolTickService_)) {
            toolTickServiceRegistered_ = true;
        }
        else {
            LOG_WARN("Failed to register the tool tick service");
        }

        lotRepository_       = std::make_unique<LotRepository>();
        propRepository_      = std::make_unique<PropRepository>();
        floraRepository_     = std::make_unique<FloraRepository>();
        favoritesRepository_ = std::make_unique<FavoritesRepository>(*propRepository_);
        packCatalog_         = std::make_unique<PackCatalogRepository>();
        packCatalog_->Load();
        decalRepository_     = std::make_unique<DecalRepository>(packCatalog_.get());
        buildingStyleService_ = std::make_unique<BuildingStyleService>();
        buildingStyleService_->Acquire(mpCOM);

        if (mpFrameWork->GetSystemService(kTerrainDecalServiceID, GZIID_cIGZTerrainDecalService,
                                          reinterpret_cast<void**>(&terrainDecalService_))) {
            LOG_INFO("Acquired terrain decal service");
        }
        else {
            LOG_WARN("Terrain decal service not available — decal painting disabled");
        }

        // Ground paint keeps its objects even without the paint service, so a city saved with
        // them does not lose them; they are just not drawn.
        groundDocument_ = std::make_unique<Ground::GroundPaintDocument>(Ground::kWhiteTextureInstance);
        if (terrainDecalService_ &&
            terrainDecalService_->QueryInterface(GZIID_cIGZTerrainPaintService,
                                                 reinterpret_cast<void**>(&terrainPaintService_))) {
            groundDocument_->SetPaintService(terrainPaintService_);
            LOG_INFO("Acquired terrain paint service");
        }
        else {
            LOG_WARN("Terrain paint not available (SC4RenderServices too old?) — ground painting disabled");
        }

        lotRepository_->Load();
        propRepository_->Load();
        floraRepository_->Load();
        favoritesRepository_->Load();
        // Decals have their own defaults, so only what was chosen replaces them.
        const PaintGridChoice& decalGrid = favoritesRepository_->GetGridChoices().decals;
        lastDecalPlacementSettings_.gridStepMeters = decalGrid.step.value_or(lastDecalPlacementSettings_.gridStepMeters);
        lastDecalPlacementSettings_.showGrid = decalGrid.show.value_or(lastDecalPlacementSettings_.showGrid);
        lastDecalPlacementSettings_.snapPointsToGrid = decalGrid.snap.value_or(lastDecalPlacementSettings_.snapPointsToGrid);
        propRepository_->ApplyUserSeasonalSets(favoritesRepository_->GetUserSeasonalSets());

        cIGZPersistResourceManagerPtr pRMForPanel;
        panel_ = std::make_unique<PlopAndPaintPanel>(
            this,
            lotRepository_.get(),
            propRepository_.get(),
            floraRepository_.get(),
            favoritesRepository_.get(),
            decalRepository_.get(),
            buildingStyleService_.get(),
            static_cast<cIGZPersistResourceManager*>(pRMForPanel),
            imguiService_);

        const ImGuiPanelDesc desc = ImGuiPanelAdapter<PlopAndPaintPanel>::MakeDesc(
            panel_.get(), kLotPlopPanelId, 100, true
        );

        if (imguiService_->RegisterPanel(desc)) {
            panelRegistered_ = true;
            panel_->SetOpen(false);
            LOG_INFO("Registered ImGui panel");
        }

        statusPanel_ = std::make_unique<PaintStatusPanel>();
        const ImGuiPanelDesc statusDesc = ImGuiPanelAdapter<PaintStatusPanel>::MakeDesc(
            statusPanel_.get(), kStatusPanelId, 101, true
        );
        if (imguiService_->RegisterPanel(statusDesc)) {
            statusPanelRegistered_ = true;
            LOG_INFO("Registered status panel");
        }

        pickStatusPanel_ = std::make_unique<ScenePickStatusPanel>();
        pickStatusPanel_->SetImGuiService(imguiService_);
        pickStatusPanel_->SetRepositories(propRepository_.get(), floraRepository_.get(), lotRepository_.get());
        const ImGuiPanelDesc pickStatusDesc = ImGuiPanelAdapter<ScenePickStatusPanel>::MakeDesc(
            pickStatusPanel_.get(), kScenePickStatusPanelId, 103, true
        );
        if (imguiService_->RegisterPanel(pickStatusDesc)) {
            pickStatusPanelRegistered_ = true;
            LOG_INFO("Registered scene pick status panel");
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

    if (pMS2_) {
        pMS2_->RemoveNotification(this, kSC4MessagePostCityInit);
        pMS2_->RemoveNotification(this, kSC4MessagePreCityShutdown);
        pMS2_->RemoveNotification(this, kSC4MessagePostCityInitComplete);
        pMS2_->RemoveNotification(this, kMessageBuildingStyleCheckboxChanged);
        pMS2_->RemoveNotification(this, kSC4MessagePostRegionInit);
        pMS2_->RemoveNotification(this, kSC4MessageLoad);
        pMS2_->RemoveNotification(this, kSC4MessageSave);
        pMS2_.Reset();
    }

    if (auto* framework = RZGetFrameWork()) {
        framework->RemoveHook(this);
    }

    // Destroy objects that may call ImGuiService first.
    if (scenePickerControl_) {
        StopPropPicking();
        if (pickStatusPanel_) {
            pickStatusPanel_->SetPicker(nullptr);
        }
        scenePickerControl_->SetCity(nullptr);
        scenePickerControl_->Shutdown();
        scenePickerControl_.Reset();
    }

    if (decalStripperControl_) {
        StopDecalStripping();
        decalStripperControl_->SetDecalService(nullptr);
        decalStripperControl_->SetCity(nullptr);
        decalStripperControl_->Shutdown();
        decalStripperControl_.Reset();
    }

    if (decalPainterControl_) {
        StopDecalPainting();
        decalPainterControl_->SetCity(nullptr);
        decalPainterControl_->Shutdown();
        decalPainterControl_.Reset();
    }

    if (groundPainterControl_) {
        StopGroundPainting();
        groundPainterControl_->SetCity(nullptr);
        groundPainterControl_->SetDocument(nullptr);
        groundPainterControl_->Shutdown();
        groundPainterControl_.Reset();
    }

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

    if (imguiService_ && pickStatusPanelRegistered_) {
        imguiService_->UnregisterPanel(kScenePickStatusPanelId);
        pickStatusPanelRegistered_ = false;
    }
    pickStatusPanel_.reset();

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

    if (buildingStyleService_) {
        buildingStyleService_->Shutdown();
    }
    buildingStyleService_.reset();

    favoritesRepository_.reset();
    floraRepository_.reset();
    propRepository_.reset();
    lotRepository_.reset();
    decalRepository_.reset();
    packCatalog_.reset();

    if (toolTickServiceRegistered_ && mpFrameWork) {
        mpFrameWork->RemoveFromTick(&toolTickService_);
        mpFrameWork->RemoveSystemService(&toolTickService_);
        toolTickServiceRegistered_ = false;
    }
    toolTickService_.Shutdown();

    if (imguiService_) {
        imguiService_->Release();
        imguiService_ = nullptr;
    }

    if (worldDrawService_) {
        worldDrawService_->Release();
        worldDrawService_ = nullptr;
    }

    if (groundDocument_) {
        groundDocument_->SetPaintService(nullptr);
        groundDocument_.reset();
    }
    if (terrainPaintService_) {
        terrainPaintService_->Release();
        terrainPaintService_ = nullptr;
    }

    if (terrainDecalService_) {
        terrainDecalService_->Release();
        terrainDecalService_ = nullptr;
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
    case kSC4MessagePostCityInitComplete: PostCityInitComplete_();
        break;
    case kMessageBuildingStyleCheckboxChanged: BuildingStyleCheckboxChanged_();
        break;
    case kSC4MessagePostRegionInit: UpdateCheck::ShowNotice();
        break;
    case kSC4MessageLoad: LoadCity_(pStandardMsg);
        break;
    case kSC4MessageSave: SaveCity_(pStandardMsg);
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
        pView3D_ && RaiseToTop_(propPainterControl_, propPainterControl_->IsActive());

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
    propPainterControl_->SetGroundShapeLookup(MakeGroundShapeLookup_());
    propPainterControl_->SetHasPaintedGround([this]() { return HasPaintedGround(); });
    propPainterControl_->SetCity(pCity_);
    propPainterControl_->SetWindow(pView3D_->AsIGZWin());
    propPainterControl_->SetWorldDrawService(GetOverlayWorldDrawService_());
    propPainterControl_->SetPropRepository(propRepository_.get());
    propPainterControl_->SetOnGridChanged([this]() { SaveGridChoice(&PaintGridChoices::props, propPainterControl_->GetSettings()); });
    propPainterControl_->SetOnCancel([this]() {
        if (pView3D_ && propPainterControl_ &&
            pView3D_->GetCurrentViewInputControl() == propPainterControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        propPainting_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped prop painting");
    });

    propPainterControl_->SetPropToPaint(propId, normalizedSettings, name);
    if (!reusingActivePropControl) {
        if (!RaiseToTop_(propPainterControl_, propPainterControl_->IsActive()) &&
            !pView3D_->SetCurrentViewInputControl(
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
        if (RaiseToTop_(propPainterControl_, propPainterControl_->IsActive())) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }

    propPainting_ = false;
    UpdatePaintPanels_();
    LOG_INFO("Stopped prop painting");
}

bool SC4PlopAndPaintDirector::IsPropPainting() const {
    return propPainting_;
}

bool SC4PlopAndPaintDirector::SwitchFloraPaintingTarget(const uint32_t floraTypeId, const std::string& name,
                                                        std::vector<FamilyEntry> palette,
                                                        const std::optional<RecentPaintSource>& source) {
    if (!floraPlacerControl_ || !IsFloraPainting() || !pView3D_ ||
        pView3D_->GetCurrentViewInputControl() != floraPlacerControl_) {
        return false;
    }
    PropPaintSettings settings = floraPlacerControl_->GetSettings();
    settings.activePalette = std::move(palette);
    return StartFloraPainting(floraTypeId, settings, name, source);
}

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
        pView3D_ && RaiseToTop_(floraPlacerControl_, floraPlacerControl_->IsActive());

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
    floraPlacerControl_->SetGroundShapeLookup(MakeGroundShapeLookup_());
    floraPlacerControl_->SetHasPaintedGround([this]() { return HasPaintedGround(); });
    floraPlacerControl_->SetCity(pCity_);
    floraPlacerControl_->SetWindow(pView3D_->AsIGZWin());
    floraPlacerControl_->SetWorldDrawService(GetOverlayWorldDrawService_());
    floraPlacerControl_->SetFloraRepository(floraRepository_.get());
    floraPlacerControl_->SetOnGridChanged([this]() { SaveGridChoice(&PaintGridChoices::flora, floraPlacerControl_->GetSettings()); });
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
        if (!RaiseToTop_(floraPlacerControl_, floraPlacerControl_->IsActive()) &&
            !pView3D_->SetCurrentViewInputControl(
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
        if (RaiseToTop_(floraPlacerControl_, floraPlacerControl_->IsActive())) {
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
    floraStripperControl_->SetWorldDrawService(GetOverlayWorldDrawService_());
    floraStripperControl_->SetOnCancel([this]() {
        if (pView3D_ && floraStripperControl_ &&
            pView3D_->GetCurrentViewInputControl() == floraStripperControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        floraStripping_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped flora stripping");
    });

    if (!RaiseToTop_(floraStripperControl_, floraStripperControl_->IsActive()) &&
        !pView3D_->SetCurrentViewInputControl(
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
        if (RaiseToTop_(floraStripperControl_, floraStripperControl_->IsActive())) {
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

bool SC4PlopAndPaintDirector::StartDecalPainting(const uint32_t instanceId,
                                                  const DecalPaintSettings& settings,
                                                  const std::string& name) {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start decal painting: city or view not available");
        return false;
    }

    if (!terrainDecalService_) {
        LOG_WARN("Cannot start decal painting: terrain decal service not available");
        return false;
    }

    RememberDecalPlacementSettings_();
    DecalPaintSettings effectiveSettings = settings;
    effectiveSettings.placementSettings = lastDecalPlacementSettings_;

    if (!PrepareForExclusiveActivation_(false, false, false, false, true, false)) {
        LOG_INFO("Blocked decal paint switch while another tool still has a sketch in progress");
        return false;
    }

    if (!decalPainterControl_) {
        auto* control = new DecalPainterInputControl();
        decalPainterControl_ = control;
        if (!decalPainterControl_->Init()) {
            LOG_ERROR("Failed to initialize DecalPainterInputControl");
            decalPainterControl_.Reset();
            return false;
        }
    }

    const bool reusingActiveDecalControl =
        decalPainting_ && decalPainterControl_ &&
        pView3D_ && RaiseToTop_(decalPainterControl_, decalPainterControl_->IsActive());

    if (reusingActiveDecalControl) {
        if (decalPainterControl_->HasPendingSketch()) {
            LOG_INFO("Blocked decal paint switch while the current sketch is still in progress");
            return false;
        }
        ApplySwitchPolicy_(decalPainterControl_);
    }

    decalPainterControl_->SetCity(pCity_);
    decalPainterControl_->SetWindow(pView3D_->AsIGZWin());
    decalPainterControl_->SetWorldDrawService(GetOverlayWorldDrawService_());
    decalPainterControl_->SetDecalService(terrainDecalService_);
    decalPainterControl_->SetOnGridChanged([this]() { SaveGridChoice(&PaintGridChoices::decals, decalPainterControl_->GetSettings()); });
    decalPainterControl_->SetOnCancel([this]() {
        if (pView3D_ && decalPainterControl_ &&
            pView3D_->GetCurrentViewInputControl() == decalPainterControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        RememberDecalPlacementSettings_();
        decalPainting_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped decal painting");
    });

    decalPainterControl_->SetDecalToPaint(instanceId, effectiveSettings, name);

    if (!reusingActiveDecalControl) {
        if (!RaiseToTop_(decalPainterControl_, decalPainterControl_->IsActive()) &&
            !pView3D_->SetCurrentViewInputControl(
                decalPainterControl_,
                cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
            LOG_WARN("Failed to set decal painter as current view input control");
            UpdatePaintPanels_();
            return false;
        }
    }

    decalPainting_ = true;
    UpdatePaintPanels_();
    LOG_INFO("Started decal painting: 0x{:08X}", instanceId);
    return true;
}

void SC4PlopAndPaintDirector::StopDecalPainting() {
    RememberDecalPlacementSettings_();

    if (decalPainterControl_) {
        decalPainterControl_->CancelAllPlacements();
    }

    if (pView3D_ && decalPainterControl_) {
        if (RaiseToTop_(decalPainterControl_, decalPainterControl_->IsActive())) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }

    decalPainting_ = false;
    UpdatePaintPanels_();
    LOG_INFO("Stopped decal painting");
}

bool SC4PlopAndPaintDirector::IsDecalPainting() const {
    return decalPainting_;
}

const TerrainDecalState* SC4PlopAndPaintDirector::GetLastDecalPaintState() const {
    return decalPainterControl_ ? &decalPainterControl_->GetState() : nullptr;
}

bool SC4PlopAndPaintDirector::StartDecalStripping() {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start decal stripping: city or view not available");
        return false;
    }

    if (!terrainDecalService_) {
        LOG_WARN("Cannot start decal stripping: terrain decal service not available");
        return false;
    }

    if (!PrepareForExclusiveActivation_(false, false, false, false, false, true)) {
        LOG_INFO("Blocked decal stripping while another tool still has a sketch in progress");
        return false;
    }

    if (!decalStripperControl_) {
        auto* control = new DecalStripperInputControl();
        decalStripperControl_ = control;
        if (!decalStripperControl_->Init()) {
            LOG_ERROR("Failed to initialize DecalStripperInputControl");
            decalStripperControl_.Reset();
            return false;
        }
    }

    decalStripperControl_->SetDecalService(terrainDecalService_);
    decalStripperControl_->SetCity(pCity_);
    decalStripperControl_->SetWindow(pView3D_->AsIGZWin());
    decalStripperControl_->SetWorldDrawService(GetOverlayWorldDrawService_());
    decalStripperControl_->SetOnCancel([this]() {
        if (pView3D_ && decalStripperControl_ &&
            pView3D_->GetCurrentViewInputControl() == decalStripperControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        decalStripping_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped decal stripping");
    });

    if (!RaiseToTop_(decalStripperControl_, decalStripperControl_->IsActive()) &&
        !pView3D_->SetCurrentViewInputControl(
            decalStripperControl_,
            cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        LOG_WARN("Failed to set decal stripper as current view input control");
        UpdatePaintPanels_();
        return false;
    }

    decalStripping_ = true;
    LOG_INFO("Started decal stripping");
    return true;
}

void SC4PlopAndPaintDirector::StopDecalStripping() {
    if (pView3D_ && decalStripperControl_) {
        if (RaiseToTop_(decalStripperControl_, decalStripperControl_->IsActive())) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }
    decalStripping_ = false;
    UpdatePaintPanels_();
    LOG_INFO("Stopped decal stripping");
}

bool SC4PlopAndPaintDirector::IsDecalStripping() const {
    return decalStripping_;
}

bool SC4PlopAndPaintDirector::StartDecalPicking(std::function<void(uint32_t)> onPick) {
    if (!terrainDecalService_) {
        LOG_WARN("Cannot start decal picking: terrain decal service not available");
        return false;
    }

    return StartScenePicking_(
        std::make_unique<DecalPickStrategy>(terrainDecalService_, decalRepository_.get()),
        [callback = std::move(onPick)](const ScenePickResult& result) mutable {
            if (const auto* decal = std::get_if<PickedDecal>(&result)) {
                callback(decal->instanceId);
            }
        },
        "decal");
}

void SC4PlopAndPaintDirector::StopDecalPicking() {
    if (pView3D_ && scenePickerControl_) {
        if (RaiseToTop_(scenePickerControl_, scenePickerControl_->IsActive())) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }
    scenePicking_ = false;
    LOG_INFO("Stopped scene picking");
}

bool SC4PlopAndPaintDirector::IsDecalPicking() const {
    return scenePicking_ && scenePickerControl_ && scenePickerControl_->GetMode() == ScenePickMode::Decal;
}

bool SC4PlopAndPaintDirector::IsDecalServiceAvailable() const {
    return terrainDecalService_ != nullptr;
}

bool SC4PlopAndPaintDirector::StartScenePicking_(std::unique_ptr<ScenePickStrategy> strategy,
                                                 std::function<void(const ScenePickResult&)> onPick,
                                                 const char* const logName) {
    if (!pCity_ || !pView3D_) {
        LOG_WARN("Cannot start {} picking: city or view not available", logName);
        return false;
    }

    if (!PrepareForExclusiveActivation_(false, false, false, false, false, false, true)) {
        LOG_INFO("Blocked {} picking while another tool still has a sketch in progress", logName);
        return false;
    }

    if (!scenePickerControl_) {
        auto* control = new ScenePickerInputControl();
        scenePickerControl_ = control;
        if (!scenePickerControl_->Init()) {
            LOG_ERROR("Failed to initialize ScenePickerInputControl");
            scenePickerControl_.Reset();
            return false;
        }
    }

    if (pickStatusPanel_) {
        pickStatusPanel_->SetPicker(scenePickerControl_);
    }

    scenePickerControl_->SetCity(pCity_);
    scenePickerControl_->SetWindow(pView3D_->AsIGZWin());
    scenePickerControl_->SetWorldDrawService(GetOverlayWorldDrawService_());
    scenePickerControl_->SetStrategy(std::move(strategy));
    scenePickerControl_->SetOnPick(std::move(onPick));
    scenePickerControl_->SetOnCancel([this]() {
        if (pView3D_ && scenePickerControl_ &&
            pView3D_->GetCurrentViewInputControl() == scenePickerControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        scenePicking_ = false;
        LOG_INFO("Stopped scene picking");
    });

    if (!RaiseToTop_(scenePickerControl_, scenePickerControl_->IsActive()) &&
        !pView3D_->SetCurrentViewInputControl(
            scenePickerControl_,
            cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        LOG_WARN("Failed to set scene picker as current view input control");
        return false;
    }

    scenePicking_ = true;
    LOG_INFO("Started {} picking", logName);
    return true;
}

bool SC4PlopAndPaintDirector::StartPropPicking(std::function<void(const PickedProp&)> onPick) {
    if (GetPropStripperSources() == PropStripperInputControl::SourceFlagNone) {
        LOG_WARN("Cannot start prop picking: no prop sources enabled");
        return false;
    }

    return StartScenePicking_(
        std::make_unique<PropPickStrategy>(GetPropStripperSources(), propRepository_.get()),
        [callback = std::move(onPick)](const ScenePickResult& result) mutable {
            if (const auto* prop = std::get_if<PickedProp>(&result)) {
                callback(*prop);
            }
        },
        "prop");
}

void SC4PlopAndPaintDirector::StopPropPicking() {
    if (pView3D_ && scenePickerControl_) {
        if (RaiseToTop_(scenePickerControl_, scenePickerControl_->IsActive())) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
    }
    scenePicking_ = false;
    LOG_INFO("Stopped scene picking");
}

bool SC4PlopAndPaintDirector::IsPropPicking() const {
    return scenePicking_ && scenePickerControl_ && scenePickerControl_->GetMode() == ScenePickMode::Prop;
}

bool SC4PlopAndPaintDirector::StartLotPicking(std::function<void(const PickedLot&)> onPick) {
    return StartScenePicking_(
        std::make_unique<LotPickStrategy>(),
        [callback = std::move(onPick)](const ScenePickResult& result) mutable {
            if (const auto* lot = std::get_if<PickedLot>(&result)) {
                callback(*lot);
            }
        },
        "lot");
}

void SC4PlopAndPaintDirector::StopLotPicking() {
    StopPropPicking();
}

bool SC4PlopAndPaintDirector::IsLotPicking() const {
    return scenePicking_ && scenePickerControl_ && scenePickerControl_->GetMode() == ScenePickMode::Lot;
}

bool SC4PlopAndPaintDirector::StartFloraPicking(std::function<void(const PickedFlora&)> onPick) {
    return StartScenePicking_(
        std::make_unique<FloraPickStrategy>(),
        [callback = std::move(onPick)](const ScenePickResult& result) mutable {
            if (const auto* flora = std::get_if<PickedFlora>(&result)) {
                callback(*flora);
            }
        },
        "flora");
}

void SC4PlopAndPaintDirector::StopFloraPicking() {
    StopPropPicking();
}

bool SC4PlopAndPaintDirector::IsFloraPicking() const {
    return scenePicking_ && scenePickerControl_ && scenePickerControl_->GetMode() == ScenePickMode::Flora;
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
    propStripperControl_->SetPropRepository(propRepository_.get());
    propStripperControl_->SetWindow(pView3D_->AsIGZWin());
    propStripperControl_->SetWorldDrawService(GetOverlayWorldDrawService_());
    propStripperControl_->SetOnCancel([this]() {
        if (pView3D_ && propStripperControl_ &&
            pView3D_->GetCurrentViewInputControl() == propStripperControl_) {
            pView3D_->RemoveCurrentViewInputControl(false);
        }
        propStripping_ = false;
        UpdatePaintPanels_();
        LOG_INFO("Stopped prop stripping");
    });

    if (!RaiseToTop_(propStripperControl_, propStripperControl_->IsActive()) &&
        !pView3D_->SetCurrentViewInputControl(
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
        if (RaiseToTop_(propStripperControl_, propStripperControl_->IsActive())) {
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

std::string SC4PlopAndPaintDirector::GetPropStripperStatus() const {
    return propStripperControl_ ? propStripperControl_->GetStatusText() : std::string{};
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
    if (decalPainterControl_ && currentControl == decalPainterControl_) {
        return decalPainterControl_;
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
    ApplyGridChoice(entry.kind == RecentPaintEntry::Kind::Prop ? &PaintGridChoices::props : &PaintGridChoices::flora, settings);

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

bool SC4PlopAndPaintDirector::HasPaintedGround() const {
    return groundDocument_ && !groundDocument_->Objects().empty();
}

PaintGridChoice SC4PlopAndPaintDirector::GetGridChoice(PaintGridChoice PaintGridChoices::* const kind) const {
    PaintGridChoice choice = favoritesRepository_ ? favoritesRepository_->GetGridChoices().*kind : PaintGridChoice{};
    choice.step = choice.step.value_or(defaultGridStepMeters_);
    choice.show = choice.show.value_or(defaultShowGridOverlay_);
    choice.snap = choice.snap.value_or(defaultSnapPointsToGrid_);
    choice.snapPlacements = choice.snapPlacements.value_or(defaultSnapPlacementsToGrid_);
    return choice;
}

void SC4PlopAndPaintDirector::ApplyGridChoice(PaintGridChoice PaintGridChoices::* const kind, PropPaintSettings& settings) const {
    const PaintGridChoice choice = GetGridChoice(kind);
    settings.gridStepMeters = *choice.step;
    settings.showGrid = *choice.show;
    settings.snapPointsToGrid = *choice.snap;
    settings.snapPlacementsToGrid = *choice.snap && *choice.snapPlacements;
}

void SC4PlopAndPaintDirector::SaveGridChoice(PaintGridChoice PaintGridChoices::* const kind, const PaintGridChoice& choice) {
    if (favoritesRepository_) {
        favoritesRepository_->SetGridChoice(kind, choice);
    }
}

void SC4PlopAndPaintDirector::SaveGridChoice(PaintGridChoice PaintGridChoices::* const kind, const PropPaintSettings& settings) {
    SaveGridChoice(kind, PaintGridChoice{.step = settings.gridStepMeters,
                                         .show = settings.showGrid,
                                         .snap = settings.snapPointsToGrid,
                                         .snapPlacements = settings.snapPlacementsToGrid});
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
    if (decalStripperControl_) {
        decalStripperControl_->ProcessPendingActions();
    }
    if (scenePickerControl_) {
        scenePickerControl_->ProcessPendingActions();
    }
    if (decalPainterControl_) {
        decalPainterControl_->ProcessPendingActions();
    }
    if (groundPainterControl_) {
        groundPainterControl_->ProcessPendingActions();
    }
}

cIGZWorldDrawService* SC4PlopAndPaintDirector::GetOverlayWorldDrawService_() const {
    return drawOverlayEnabled_ ? worldDrawService_ : nullptr;
}

void SC4PlopAndPaintDirector::SetLotPlopPanelVisible(const bool visible) {
    if (!imguiService_ || !panelRegistered_ || !panel_) {
        return;
    }

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

    if (pView3D_ && RaiseToTop_(control, control->IsActive())) {
        pView3D_->RemoveCurrentViewInputControl(false);
    }

    isPaintingFlag = false;
    return true;
}

bool SC4PlopAndPaintDirector::RaiseToTop_(cISC4ViewInputControl* const control, const bool active) {
    if (!pView3D_ || !control) {
        return false;
    }
    if (active) {
        while (cISC4ViewInputControl* const top = pView3D_->GetCurrentViewInputControl()) {
            if (top == control || !pView3D_->RemoveCurrentViewInputControl(false)) {
                break;
            }
        }
    }
    return pView3D_->GetCurrentViewInputControl() == control;
}

bool SC4PlopAndPaintDirector::PrepareForExclusiveActivation_(const bool keepPropPainting,
                                                             const bool keepFloraPainting,
                                                             const bool keepPropStripping,
                                                             const bool keepFloraStripping,
                                                             const bool keepDecalPainting,
                                                             const bool keepDecalStripping,
                                                             const bool keepScenePicking) {
    if (!keepPropPainting && !CanPrepareForPaintSwitch_(propPainterControl_, propPainting_)) {
        return false;
    }

    // Every other tool takes over from ground painting; it restarts itself when needed.
    if (groundPainting_) {
        StopGroundPainting();
    }

    if (!keepFloraPainting && !CanPrepareForPaintSwitch_(floraPlacerControl_, floraPainting_)) {
        return false;
    }

    if (!keepDecalPainting && !CanPrepareForPaintSwitch_(decalPainterControl_, decalPainting_)) {
        return false;
    }

    if (!keepPropPainting) {
        PrepareForPaintSwitch_(propPainterControl_, propPainting_);
    }

    if (!keepFloraPainting) {
        PrepareForPaintSwitch_(floraPlacerControl_, floraPainting_);
    }

    if (!keepDecalPainting) {
        RememberDecalPlacementSettings_();
        PrepareForPaintSwitch_(decalPainterControl_, decalPainting_);
    }

    if (!keepPropStripping && propStripping_) {
        StopPropStripping();
    }

    if (!keepFloraStripping && floraStripping_) {
        StopFloraStripping();
    }

    if (!keepDecalStripping && decalStripping_) {
        StopDecalStripping();
    }

    if (!keepScenePicking && scenePicking_) {
        StopPropPicking();
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

void SC4PlopAndPaintDirector::RememberDecalPlacementSettings_() {
    if (!decalPainterControl_) {
        return;
    }

    const PropPaintSettings& current = decalPainterControl_->GetSettings();
    PropPaintSettings remembered = MakeDefaultDecalPlacementSettings();
    remembered.previewMode = current.previewMode;
    remembered.showGrid = current.showGrid;
    remembered.snapPointsToGrid = current.snapPointsToGrid;
    remembered.gridStepMeters = current.gridStepMeters;
    lastDecalPlacementSettings_ = remembered;
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

    SC4GraphicsDriverInfo driver{};
    if (imguiService_ && imguiService_->GetActiveDriverInfo(driver)) {
        LOG_INFO("Graphics driver: {}", driver.name);
    }

    if (decalRepository_) {
        cIGZPersistResourceManagerPtr pRM;
        if (pRM) {
            decalRepository_->Populate(pRM);
        }
    }

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

    // The objects came in with the save; give those whose terrain paint is missing a new one.
    if (groundDocument_) {
        groundDocument_->Restore();
    }
}

void SC4PlopAndPaintDirector::PostCityInitComplete_() {
    if (buildingStyleService_) {
        if (!buildingStyleService_->HasStyleInfoApi()) {
            buildingStyleService_->Acquire(mpCOM);
        }
        buildingStyleService_->RefreshForCity(pCity_);
    }
}

void SC4PlopAndPaintDirector::BuildingStyleCheckboxChanged_() {
    if (buildingStyleService_) {
        // Re-read the tract developer instead of applying the message delta so
        // the cache stays correct if notifications were missed or reordered.
        buildingStyleService_->RefreshActiveStyles(pCity_);
    }
}

void SC4PlopAndPaintDirector::PreCityShutdown_(cIGZMessage2Standard* pStandardMsg) {
    PersistRecentPaints_();
    SetLotPlopPanelVisible(false);
    StopDecalStripping();
    StopDecalPainting();
    StopGroundPainting();
    StopFloraStripping();
    StopPropStripping();
    StopPropPicking();
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
    if (decalPainterControl_) {
        decalPainterControl_->SetCity(nullptr);
    }
    if (decalStripperControl_) {
        decalStripperControl_->SetDecalService(nullptr);
        decalStripperControl_->SetCity(nullptr);
    }
    if (scenePickerControl_) {
        scenePickerControl_->SetCity(nullptr);
    }
    if (decalRepository_) {
        decalRepository_->Clear();
    }
    if (buildingStyleService_) {
        buildingStyleService_->ClearCityState();
    }
    if (groundPainterControl_) {
        groundPainterControl_->SetCity(nullptr);
    }
    if (groundDocument_) {
        groundDocument_->Reset();
        unreadableGroundPaint_.clear();
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
    SetLotPlopPanelVisible(!(panel_ && panel_->IsOpen()));
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

bool SC4PlopAndPaintDirector::IsGroundPaintAvailable() const {
    return terrainPaintService_ != nullptr && groundDocument_ != nullptr;
}

bool SC4PlopAndPaintDirector::StartGroundPainting(const Ground::ToolSettings& settings) {
    if (!pCity_ || !pView3D_ || !IsGroundPaintAvailable()) {
        LOG_WARN("Cannot start ground painting: city, view or terrain paint service not available");
        return false;
    }
    if (groundPainting_) {
        UpdateGroundPainting(settings);
        return true;
    }
    if (!PrepareForExclusiveActivation_(false, false, false, false, false, false, false)) {
        LOG_INFO("Blocked ground painting while another tool still has a sketch in progress");
        return false;
    }

    if (!groundPainterControl_) {
        groundPainterControl_ = new GroundPainterInputControl();
        if (!groundPainterControl_->Init()) {
            LOG_ERROR("Failed to initialize GroundPainterInputControl");
            groundPainterControl_.Reset();
            return false;
        }
    }

    groundPainterControl_->SetCity(pCity_);
    groundPainterControl_->SetWindow(pView3D_->AsIGZWin());
    groundPainterControl_->SetWorldDrawService(GetOverlayWorldDrawService_());
    groundPainterControl_->SetDocument(groundDocument_.get());
    groundPainterControl_->SetSettings(settings);
    groundPainterControl_->SetOnCancel([this]() { StopGroundPainting(); });
    groundPainterControl_->SetOnGridChanged([this]() {
        const Ground::ToolSettings& current = groundPainterControl_->Settings();
        SaveGridChoice(&PaintGridChoices::ground, PaintGridChoice{.step = current.gridStepMeters,
                                                                  .show = current.showGrid,
                                                                  .snap = current.snapPointsToGrid});
    });

    if (!RaiseToTop_(groundPainterControl_, groundPainterControl_->IsActive()) &&
        !pView3D_->SetCurrentViewInputControl(
            groundPainterControl_,
            cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        LOG_WARN("Failed to set the ground painter as current view input control");
        return false;
    }

    groundPainting_ = true;
    LOG_INFO("Started ground painting");
    return true;
}

void SC4PlopAndPaintDirector::UpdateGroundPainting(const Ground::ToolSettings& settings) {
    if (groundPainterControl_) {
        groundPainterControl_->SetSettings(settings);
    }
}

void SC4PlopAndPaintDirector::StopGroundPainting() {
    if (!groundPainting_) {
        return;
    }
    groundPainting_ = false;
    if (pView3D_ && groundPainterControl_ && RaiseToTop_(groundPainterControl_, groundPainterControl_->IsActive())) {
        pView3D_->RemoveCurrentViewInputControl(false);
    }
    LOG_INFO("Stopped ground painting");
}

bool SC4PlopAndPaintDirector::IsGroundPainting() const {
    return groundPainting_;
}

bool SC4PlopAndPaintDirector::IsAnyToolActive() const {
    return propPainting_ || floraPainting_ || floraStripping_ || decalPainting_ || decalStripping_ || propStripping_ ||
           groundPainting_ || scenePicking_;
}

Ground::GroundPaintDocument* SC4PlopAndPaintDirector::GetGroundDocument() const {
    return groundDocument_.get();
}

const GroundPainterInputControl* SC4PlopAndPaintDirector::GetGroundPainter() const {
    return groundPainting_ ? static_cast<const GroundPainterInputControl*>(groundPainterControl_) : nullptr;
}

GroundPainterInputControl* SC4PlopAndPaintDirector::GetGroundPainter() {
    return const_cast<GroundPainterInputControl*>(std::as_const(*this).GetGroundPainter());
}

std::optional<BasePainterInputControl::GroundShape> SC4PlopAndPaintDirector::GroundShapeOf_(const uint32_t objectId,
                                                                                           const bool along) const {
    const Ground::PaintObject* const object = groundDocument_ ? groundDocument_->Find(objectId) : nullptr;
    if (!object) {
        return std::nullopt;
    }
    const Ground::Area& area = groundDocument_->AreaOf(objectId);
    Ground::PlantingOutline outline = Ground::PlantingOutlineOf(*object, area, along);
    if (outline.points.size() < (along ? 2u : 3u)) {
        return std::nullopt;
    }
    return BasePainterInputControl::GroundShape{std::move(outline.points), outline.closed, along ? Ground::Area{} : area};
}

BasePainterInputControl::GroundShapeLookup SC4PlopAndPaintDirector::MakeGroundShapeLookup_() const {
    return [this](const Ground::Vec2 point, const bool along) -> std::optional<BasePainterInputControl::GroundShape> {
        const auto hit = groundDocument_ ? groundDocument_->HitTest(point) : std::nullopt;
        return hit ? GroundShapeOf_(*hit, along) : std::nullopt;
    };
}

bool SC4PlopAndPaintDirector::PlantOnGround(const uint32_t objectId, const bool along) {
    if (recentPaints_.Empty()) {
        LOG_INFO("Nothing to plant on painted ground: no prop or flora painted yet");
        return false;
    }
    const std::optional<BasePainterInputControl::GroundShape> shape = GroundShapeOf_(objectId, along);
    if (!shape) {
        return false;
    }
    const RecentPaintEntry entry = recentPaints_.Entries().front();
    const bool prop = entry.kind == RecentPaintEntry::Kind::Prop;
    // The settings the painter last had, in the mode the shape needs.
    BasePainterInputControl* const control =
        prop ? static_cast<BasePainterInputControl*>(propPainterControl_) : floraPlacerControl_;
    PropPaintSettings settings = control ? control->GetSettings() : PropPaintSettings{};
    SetPaintMode(settings, along ? PaintMode::Line : PaintMode::Polygon);
    settings.pickFromGround = false;
    ApplyGridChoice(prop ? &PaintGridChoices::props : &PaintGridChoices::flora, settings);
    settings.activePalette = entry.palette;
    settings.randomSeed = 0;
    const RecentPaintSource source{.sourceKind = entry.sourceKind, .sourceId = entry.sourceId};
    const bool started = prop ? StartPropPainting(entry.typeId, settings, entry.name, source)
                              : StartFloraPainting(entry.typeId, settings, entry.name, source);
    BasePainterInputControl* const painter =
        prop ? static_cast<BasePainterInputControl*>(propPainterControl_) : floraPlacerControl_;
    return started && painter && painter->UseGroundShape(*shape);
}

namespace {
    // The city save segment a load or save message carries, AddRef'd.
    cISC4DBSegment* SaveSegment(const cIGZMessage2Standard* const message) {
        auto* const unknown = message ? static_cast<cIGZUnknown*>(message->GetVoid1()) : nullptr;
        cISC4DBSegment* segment = nullptr;
        if (!unknown || !unknown->QueryInterface(GZIID_cISC4DBSegment, reinterpret_cast<void**>(&segment))) {
            return nullptr;
        }
        return segment;
    }
}

void SC4PlopAndPaintDirector::LoadCity_(const cIGZMessage2Standard* const pStandardMsg) {
    if (!groundDocument_) {
        return;
    }
    groundDocument_->Reset();
    unreadableGroundPaint_.clear();
    cISC4DBSegment* const segment = SaveSegment(pStandardMsg);
    if (!segment) {
        return;
    }
    if (const auto payload = Ground::SaveRecord::Read(*segment)) {
        if (groundDocument_->Load(*payload)) {
            LOG_INFO("Loaded {} ground paint objects", groundDocument_->Objects().size());
        }
        else {
            LOG_WARN("Ground paint: could not read the objects in the save. They are kept unless you paint in this city.");
            unreadableGroundPaint_ = *payload;
        }
    }
    segment->Release();
}

void SC4PlopAndPaintDirector::SaveCity_(const cIGZMessage2Standard* const pStandardMsg) const {
    if (!groundDocument_) {
        return;
    }
    cISC4DBSegment* const segment = SaveSegment(pStandardMsg);
    if (!segment) {
        return;
    }
    const std::vector<char> payload = groundDocument_->Objects().empty() ? unreadableGroundPaint_ : groundDocument_->Save();
    Ground::SaveRecord::Write(*segment, payload);
    segment->Release();
}
