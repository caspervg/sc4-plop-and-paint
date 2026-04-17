#include "OverlayManagerDebugPanel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "SC4PlopAndPaintDirector.hpp"
#include "cGZPersistResourceKey.h"
#include "cIGZCOM.h"
#include "cIGZFrameWork.h"
#include "cISC4City.h"
#include "cISC4Occupant.h"
#include "cISC4OccupantManager.h"
#include "cISC4PropOccupant.h"
#include "cISC4PropOccupantTerrainDecal.h"
#include "cISTEOverlayManager.h"
#include "cISTETerrain.h"
#include "cISTETerrainView.h"
#include "cRZAutoRefCount.h"
#include "cRZCOMDllDirector.h"
#include "cS3DVector2.h"
#include "cS3DVector3.h"
#include "imgui.h"
#include "terrain/TerrainDecalHook.hpp"
#include "utils/Logger.h"

namespace DebugUi
{
    class OverlayManagerDebugPanelState final
    {
    public:
        using RuntimeDecalSnapshot = PlopAndPaint::Sidecar::SidecarSaveHook::RuntimeDecalSnapshot;

        struct CreatedOverlayEntry
        {
            uint32_t overlayId = 0;
            uint32_t textureGroup = 0;
            uint32_t textureInstance = 0;
            bool ring = false;
        };

        explicit OverlayManagerDebugPanelState(SC4PlopAndPaintDirector* director)
            : director_(director)
        {
        }

        enum class SelectedTargetKind
        {
            Overlay,
            Prop,
        };

        void OnInit()
        {
            observedCity_ = director_ ? director_->GetCity() : nullptr;
            LOG_INFO("OverlayManagerDebugPanel: initialized");
        }

        void OnRender()
        {
            HandleCityChange_();

            cISTEOverlayManager* overlay = ResolveOverlayManager_();
            TerrainDecal::TerrainDecalHook* hook = director_ ? director_->GetTerrainDecalHook() : nullptr;
            PlopAndPaint::Sidecar::SidecarSaveHook* sidecar = director_ ? director_->GetSidecarSaveHook() : nullptr;
            const auto runtimeDecals = sidecar ? sidecar->SnapshotRuntimeDecals() : std::vector<RuntimeDecalSnapshot>{};
            const std::optional<RuntimeDecalSnapshot> selectedRuntimeDecal =
                sidecar ? FindSelectedRuntimeDecal_(runtimeDecals) : std::nullopt;
            const uint32_t hookOverlayKey = NormalizeOverlayIdForHook_(overlayId_);

            ImGui::Begin("Terrain Decal Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

            ImGui::Text("Overlay manager: %s", overlay ? "available" : "unavailable");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            ImGui::Combo("##overlayType", &overlayType_, kOverlayTypeNames, IM_ARRAYSIZE(kOverlayTypeNames));

            ImGui::SeparatorText("Create");
            ImGui::Text("Texture Type: %08X", textureType_);
            ImGui::InputScalar("Texture Group (hex)", ImGuiDataType_U32, &textureGroup_, nullptr, nullptr, "%08X");
            ImGui::InputScalar("Texture Instance (hex)", ImGuiDataType_U32, &textureInstance_, nullptr, nullptr, "%08X");
            ImGui::InputFloat2("Center (x,z)", &center_[0]);
            ImGui::Checkbox("Snap center to tile center", &snapCenterToTile_);
            ImGui::SameLine();
            if (ImGui::Button("Snap Center")) {
                center_[0] = SnapToTileCenter_(center_[0]);
                center_[1] = SnapToTileCenter_(center_[1]);
            }
            ImGui::InputFloat("Base size", &baseSize_, 0.1f, 1.0f, "%.2f");
            ImGui::InputFloat("Rotation turns", &rotationTurns_, 0.01f, 0.1f, "%.3f");
            ImGui::InputFloat("Decal opacity", &alpha_, 0.05f, 0.5f, "%.2f");

            const bool overlayAvailable = overlay != nullptr;
            if (!overlayAvailable) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Add Decal")) {
                const auto snappedCenter = GetSnappedCenter_(center_);
                const cGZPersistResourceKey textureKey(textureType_, textureGroup_, textureInstance_);
                lastOverlayId_ = overlay->AddDecal(textureKey,
                                                   cS3DVector2{snappedCenter[0], snappedCenter[1]},
                                                   baseSize_,
                                                   rotationTurns_);
                overlayId_ = lastOverlayId_;
                center_[0] = snappedCenter[0];
                center_[1] = snappedCenter[1];
                PushCreatedOverlay_(lastOverlayId_, false);
                status_ = "AddDecal called";
                FetchDecalInfo_(overlay);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Ring Decal")) {
                const auto snappedCenter = GetSnappedCenter_(center_);
                lastOverlayId_ = overlay->AddRingDecal(textureInstance_,
                                                       cS3DVector2{snappedCenter[0], snappedCenter[1]},
                                                       baseSize_,
                                                       rotationTurns_);
                overlayId_ = lastOverlayId_;
                center_[0] = snappedCenter[0];
                center_[1] = snappedCenter[1];
                PushCreatedOverlay_(lastOverlayId_, true);
                status_ = "AddRingDecal called";
                FetchDecalInfo_(overlay);
            }
            if (!overlayAvailable) {
                ImGui::EndDisabled();
            }

            // Decal-prop occupant path: spawns a real cISC4PropOccupantTerrainDecal
            // which SC4 serializes natively and which fires InsertOccupant, so the
            // sidecar hook can observe it. Distinct from the loose-overlay path
            // above (which is retained for renderer testing).
            const bool cityAvailable = director_ && director_->GetCity() != nullptr;
            if (!cityAvailable) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Add Decal Prop")) {
                const auto snappedCenter = GetSnappedCenter_(center_);
                if (AddDecalProp_(snappedCenter[0], snappedCenter[1])) {
                    center_[0] = snappedCenter[0];
                    center_[1] = snappedCenter[1];
                }
            }
            if (!cityAvailable) {
                ImGui::EndDisabled();
            }

            ImGui::SeparatorText("Decal Props");
            ImGui::TextWrapped("Runtime decal props are tracked by the sidecar hook and reuse the editor controls above.");
            ImGui::TextWrapped("Prop decals apply position, size, rotation, opacity, texture, and visibility live. Advanced decal-info and hook UV fields are stored in sidecar-backed runtime state for persistence.");
            ImGui::Text("Tracked decal props: %d", static_cast<int>(runtimeDecals.size()));
            if (ImGui::BeginListBox("##createdDecalProps", ImVec2(340.0f, 120.0f))) {
                for (const RuntimeDecalSnapshot& entry : runtimeDecals) {
                    char label[128];
                    std::snprintf(label,
                                  sizeof(label),
                                  "%llu | %08X:%08X | size=%.2f | alpha=%.2f",
                                  static_cast<unsigned long long>(entry.id),
                                  entry.textureKey.group,
                                  entry.textureKey.instance,
                                  entry.overlayInfo.baseSize,
                                  entry.opacity);
                    if (ImGui::Selectable(label, selectedRuntimeDecalId_ == entry.id)) {
                        selectedRuntimeDecalId_ = entry.id;
                    }
                }
                ImGui::EndListBox();
            }

            const bool hasSelectedDecalProp = selectedRuntimeDecal.has_value();
            if (!hasSelectedDecalProp) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Use Selected Prop")) {
                selectedTargetKind_ = SelectedTargetKind::Prop;
                CopyRuntimeDecalToEditor_(*selectedRuntimeDecal);
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply Selected Prop")) {
                ApplySelectedRuntimeDecalFromCreateControls_(sidecar);
            }
            ImGui::SameLine();
            if (ImGui::Button("Use Prop Hook UV")) {
                selectedTargetKind_ = SelectedTargetKind::Prop;
                FetchRuntimeDecalHookUv_(*selectedRuntimeDecal);
            }
            if (!hasSelectedDecalProp) {
                ImGui::EndDisabled();
            }

            ImGui::SeparatorText("Selection");
            ImGui::InputScalar("Overlay ID", ImGuiDataType_U32, &overlayId_, nullptr, nullptr, "%u");
            ImGui::SameLine();
            if (ImGui::Button("Use Last Created")) {
                overlayId_ = lastOverlayId_;
                selectedTargetKind_ = SelectedTargetKind::Overlay;
            }
            ImGui::Text("Hook UV key: %u (0x%08X)", hookOverlayKey, hookOverlayKey);
            ImGui::Text("Active target: %s", selectedTargetKind_ == SelectedTargetKind::Overlay ? "Overlay" : "Prop");

            ImGui::Text("Created overlays: %d", static_cast<int>(createdOverlays_.size()));
            if (ImGui::BeginListBox("##createdOverlays", ImVec2(340.0f, 120.0f))) {
                for (size_t i = 0; i < createdOverlays_.size(); ++i) {
                    const CreatedOverlayEntry& entry = createdOverlays_[i];
                    char label[96];
                    std::snprintf(label,
                                  sizeof(label),
                                  "%u | %s | %08X:%08X",
                                  entry.overlayId,
                                  entry.ring ? "Ring" : "Decal",
                                  entry.textureGroup,
                                  entry.textureInstance);
                    if (ImGui::Selectable(label, selectedCreatedOverlayIndex_ == static_cast<int>(i))) {
                        selectedCreatedOverlayIndex_ = static_cast<int>(i);
                    }
                }
                ImGui::EndListBox();
            }
            if (selectedCreatedOverlayIndex_ >= 0 &&
                selectedCreatedOverlayIndex_ < static_cast<int>(createdOverlays_.size())) {
                if (ImGui::Button("Use Selected Overlay")) {
                    const CreatedOverlayEntry& entry =
                        createdOverlays_[static_cast<size_t>(selectedCreatedOverlayIndex_)];
                    overlayId_ = entry.overlayId;
                    textureGroup_ = entry.textureGroup;
                    textureInstance_ = entry.textureInstance;
                    selectedTargetKind_ = SelectedTargetKind::Overlay;
                    status_ = "Selected overlay restored from history";
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("Clear Overlay History")) {
                createdOverlays_.clear();
                selectedCreatedOverlayIndex_ = -1;
                status_ = "Overlay history cleared";
            }

            const bool canEditSelection =
                selectedTargetKind_ == SelectedTargetKind::Overlay ? overlayAvailable : hasSelectedDecalProp;
            if (!canEditSelection) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Fetch DecalInfo")) {
                if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                    FetchDecalInfo_(overlay);
                }
                else if (selectedRuntimeDecal) {
                    FetchRuntimeDecalInfo_(*selectedRuntimeDecal);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Selected")) {
                if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                    overlay->RemoveOverlay(overlayId_);
                    if (hook) {
                        (void)hook->RemoveOverlayUvSubrect(overlayId_);
                    }
                    status_ = "RemoveOverlay called";
                }
                else {
                    (void)RemoveSelectedRuntimeDecal_(sidecar);
                }
            }

            ImGui::InputFloat2("Move to center (x,z)", &moveCenter_[0]);
            if (ImGui::Button("Move Decal")) {
                const auto snappedCenter = GetSnappedCenter_(moveCenter_);
                if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                    overlay->MoveDecal(overlayId_, cS3DVector2{snappedCenter[0], snappedCenter[1]});
                    status_ = "MoveDecal called";
                }
                else {
                    MoveSelectedRuntimeDecal_(sidecar, snappedCenter[0], snappedCenter[1]);
                }
                moveCenter_[0] = snappedCenter[0];
                moveCenter_[1] = snappedCenter[1];
            }
            ImGui::InputFloat("Overlay alpha", &alpha_, 0.05f, 0.5f, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("Set Alpha")) {
                if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                    overlay->SetOverlayAlpha(overlayId_, alpha_);
                    status_ = "SetOverlayAlpha called";
                }
                else {
                    SetSelectedRuntimeDecalAlpha_(sidecar);
                }
            }
            ImGui::Checkbox("Enabled", &enabled_);
            ImGui::SameLine();
            if (ImGui::Button("Apply Enabled")) {
                if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                    overlay->SetOverlayEnabled(overlayId_, enabled_);
                    status_ = "SetOverlayEnabled called";
                }
                else {
                    SetSelectedRuntimeDecalEnabled_(sidecar);
                }
            }
            if (!canEditSelection) {
                ImGui::EndDisabled();
            }

            ImGui::SeparatorText("Decal Info");
            bool applyDecalInfoNow = false;
            const auto markApplyOnDeactivate = [&applyDecalInfoNow]() {
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    applyDecalInfoNow = true;
                }
            };

            ImGui::InputFloat2("Info center (x,z)", &infoCenter_[0]);
            markApplyOnDeactivate();
            ImGui::InputFloat("Info base size", &infoBaseSize_, 0.1f, 1.0f, "%.2f");
            markApplyOnDeactivate();
            ImGui::InputFloat("Info rotation turns", &infoRotationTurns_, 0.01f, 0.1f, "%.3f");
            markApplyOnDeactivate();
            ImGui::InputFloat("Info aspect multiplier", &infoAspectMultiplier_, 0.01f, 0.1f, "%.3f");
            markApplyOnDeactivate();
            ImGui::InputFloat("Info UV scale U", &infoUvScaleU_, 0.01f, 0.1f, "%.3f");
            markApplyOnDeactivate();
            ImGui::InputFloat("Info UV scale V", &infoUvScaleV_, 0.01f, 0.1f, "%.3f");
            markApplyOnDeactivate();
            ImGui::InputFloat("Info UV offset", &infoUvOffset_, 0.01f, 0.1f, "%.3f");
            markApplyOnDeactivate();
            ImGui::InputFloat("Info unknown8", &infoUnknown8_, 0.01f, 0.1f, "%.3f");
            markApplyOnDeactivate();
            if (ImGui::Checkbox("Apply center via MoveDecal", &applyCenterViaMoveDecal_)) {
                applyDecalInfoNow = true;
            }

            if (!canEditSelection) {
                ImGui::BeginDisabled();
            }
            if (canEditSelection && applyDecalInfoNow) {
                if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                    ApplyDecalInfo_(overlay);
                }
                else {
                    ApplySelectedRuntimeDecalInfo_(sidecar);
                }
            }
            if (!canEditSelection) {
                ImGui::EndDisabled();
            }

            ImGui::SeparatorText("Hook UV Subrect");
            if (selectedTargetKind_ == SelectedTargetKind::Overlay && !hook) {
                ImGui::TextDisabled("Terrain decal hook unavailable.");
            }
            else {
                const bool canEditHookUv =
                    selectedTargetKind_ == SelectedTargetKind::Overlay ? hook != nullptr : hasSelectedDecalProp;
                if (!canEditHookUv) {
                    ImGui::BeginDisabled();
                }

                bool applyHookUvNow = false;
                const auto markHookUvApplyOnDeactivate = [&applyHookUvNow]() {
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        applyHookUvNow = true;
                    }
                };

                if (ImGui::Checkbox("Enable hook UV override", &hookUvOverrideEnabled_)) {
                    applyHookUvNow = true;
                }
                if (ImGui::Combo("Hook UV mode", &hookUvMode_, kHookUvModeNames, IM_ARRAYSIZE(kHookUvModeNames))) {
                    applyHookUvNow = true;
                }
                ImGui::InputFloat("U1", &hookUvSubrect_.u1, 0.01f, 0.1f, "%.3f");
                markHookUvApplyOnDeactivate();
                ImGui::InputFloat("V1", &hookUvSubrect_.v1, 0.01f, 0.1f, "%.3f");
                markHookUvApplyOnDeactivate();
                ImGui::InputFloat("U2", &hookUvSubrect_.u2, 0.01f, 0.1f, "%.3f");
                markHookUvApplyOnDeactivate();
                ImGui::InputFloat("V2", &hookUvSubrect_.v2, 0.01f, 0.1f, "%.3f");
                markHookUvApplyOnDeactivate();

                if (ImGui::Button("Fetch Hook UV")) {
                    if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                        TerrainDecal::OverlayUvSubrect currentUv{};
                        if (hook->TryGetOverlayUvSubrect(overlayId_, currentUv)) {
                            hookUvSubrect_ = currentUv;
                            hookUvMode_ = static_cast<int>(currentUv.mode);
                            hookUvOverrideEnabled_ = true;
                            LOG_INFO("OverlayManagerDebugPanel: fetched hook UV override for overlay {} (hook key {})",
                                     overlayId_,
                                     hookOverlayKey);
                            status_ = "Fetched hook UV override";
                        }
                        else {
                            hookUvSubrect_ = {};
                            hookUvMode_ = 0;
                            hookUvOverrideEnabled_ = false;
                            LOG_INFO("OverlayManagerDebugPanel: no hook UV override registered for overlay {} (hook key {})",
                                     overlayId_,
                                     hookOverlayKey);
                            status_ = "No hook UV override registered";
                        }
                    }
                    else if (selectedRuntimeDecal) {
                        FetchRuntimeDecalHookUv_(*selectedRuntimeDecal);
                    }
                }
                if (applyHookUvNow) {
                    if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                        if (hookUvOverrideEnabled_) {
                            hookUvSubrect_.mode = static_cast<TerrainDecal::OverlayUvMode>(hookUvMode_);
                            LOG_INFO("OverlayManagerDebugPanel: auto-applying hook UV override for overlay {} (hook key {}, mode={}) -> [{:.3f}, {:.3f}] to [{:.3f}, {:.3f}]",
                                     overlayId_,
                                     hookOverlayKey,
                                     kHookUvModeNames[hookUvMode_],
                                     hookUvSubrect_.u1,
                                     hookUvSubrect_.v1,
                                     hookUvSubrect_.u2,
                                     hookUvSubrect_.v2);
                            hook->SetOverlayUvSubrect(overlayId_, hookUvSubrect_);
                            status_ = "Applied hook UV override";
                        }
                        else {
                            LOG_INFO("OverlayManagerDebugPanel: auto-apply removed hook UV override for overlay {} (hook key {})",
                                     overlayId_,
                                     hookOverlayKey);
                            (void)hook->RemoveOverlayUvSubrect(overlayId_);
                            status_ = "Removed hook UV override";
                        }
                    }
                    else {
                        (void)ApplySelectedRuntimeDecalHookUv_(sidecar);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply Hook UV")) {
                    if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                        if (hookUvOverrideEnabled_) {
                            hookUvSubrect_.mode = static_cast<TerrainDecal::OverlayUvMode>(hookUvMode_);
                            LOG_INFO("OverlayManagerDebugPanel: applying hook UV override for overlay {} (hook key {}, mode={}) -> [{:.3f}, {:.3f}] to [{:.3f}, {:.3f}]",
                                     overlayId_,
                                     hookOverlayKey,
                                     kHookUvModeNames[hookUvMode_],
                                     hookUvSubrect_.u1,
                                     hookUvSubrect_.v1,
                                     hookUvSubrect_.u2,
                                     hookUvSubrect_.v2);
                            hook->SetOverlayUvSubrect(overlayId_, hookUvSubrect_);
                            status_ = "Applied hook UV override";
                        }
                        else {
                            LOG_INFO("OverlayManagerDebugPanel: apply requested with hook override disabled, removing overlay {} (hook key {})",
                                     overlayId_,
                                     hookOverlayKey);
                            (void)hook->RemoveOverlayUvSubrect(overlayId_);
                            status_ = "Removed hook UV override";
                        }
                    }
                    else {
                        (void)ApplySelectedRuntimeDecalHookUv_(sidecar);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Hook UV")) {
                    if (selectedTargetKind_ == SelectedTargetKind::Overlay) {
                        LOG_INFO("OverlayManagerDebugPanel: clearing hook UV override for overlay {} (hook key {})",
                                 overlayId_,
                                 hookOverlayKey);
                        (void)hook->RemoveOverlayUvSubrect(overlayId_);
                    }
                    else {
                        hookUvOverrideEnabled_ = false;
                        (void)ApplySelectedRuntimeDecalHookUv_(sidecar);
                    }
                    hookUvSubrect_ = {};
                    hookUvMode_ = 0;
                    hookUvOverrideEnabled_ = false;
                    status_ = "Cleared hook UV override";
                }

                if (!canEditHookUv) {
                    ImGui::EndDisabled();
                }
            }

            ImGui::Separator();
            ImGui::TextWrapped("%s", status_);
            ImGui::End();
        }

        void OnShutdown()
        {
            ClearTransientHistory_();
            LOG_INFO("OverlayManagerDebugPanel: shutdown");
        }

    private:
        cISTEOverlayManager* ResolveOverlayManager_() const
        {
            if (!director_) {
                return nullptr;
            }

            cISC4City* const city = director_->GetCity();
            cISTETerrain* const terrain = city ? city->GetTerrain() : nullptr;
            cISTETerrainView* const view = terrain ? terrain->GetView() : nullptr;
            return view
                       ? view->GetOverlayManager(static_cast<cISTETerrainView::tOverlayManagerType>(overlayType_))
                       : nullptr;
        }

        void FetchDecalInfo_(cISTEOverlayManager* overlay)
        {
            if (!overlay) {
                return;
            }

            cISTEOverlayManager::cDecalInfo info{};
            overlay->DecalInfo(overlayId_, &info);
            infoCenter_[0] = info.center.fX;
            infoCenter_[1] = info.center.fY;
            infoBaseSize_ = info.baseSize;
            infoRotationTurns_ = info.rotationTurns;
            infoAspectMultiplier_ = info.aspectMultiplier;
            infoUvScaleU_ = info.uvScaleU;
            infoUvScaleV_ = info.uvScaleV;
            infoUvOffset_ = info.uvOffset;
            infoUnknown8_ = info.unknown8;
            moveCenter_[0] = info.center.fX;
            moveCenter_[1] = info.center.fY;
            status_ = "DecalInfo fetched";
        }

        void ApplyDecalInfo_(cISTEOverlayManager* overlay)
        {
            if (!overlay) {
                return;
            }

            cISTEOverlayManager::cDecalInfo info{};
            overlay->DecalInfo(overlayId_, &info);

            const auto snappedCenter = GetSnappedCenter_(infoCenter_);
            if (!applyCenterViaMoveDecal_) {
                info.center = cS3DVector2{snappedCenter[0], snappedCenter[1]};
            }
            info.baseSize = infoBaseSize_;
            info.rotationTurns = infoRotationTurns_;
            info.aspectMultiplier = infoAspectMultiplier_;
            info.uvScaleU = infoUvScaleU_;
            info.uvScaleV = infoUvScaleV_;
            info.uvOffset = infoUvOffset_;
            info.unknown8 = infoUnknown8_;
            overlay->UpdateDecalInfo(overlayId_, info);

            if (applyCenterViaMoveDecal_) {
                overlay->MoveDecal(overlayId_, cS3DVector2{snappedCenter[0], snappedCenter[1]});
            }

            infoCenter_[0] = snappedCenter[0];
            infoCenter_[1] = snappedCenter[1];
            moveCenter_[0] = snappedCenter[0];
            moveCenter_[1] = snappedCenter[1];
            status_ = applyCenterViaMoveDecal_ ? "UpdateDecalInfo + MoveDecal called" : "UpdateDecalInfo called";
        }

        [[nodiscard]] std::array<float, 2> GetSnappedCenter_(const float source[2]) const
        {
            if (!snapCenterToTile_) {
                return {source[0], source[1]};
            }

            return {SnapToTileCenter_(source[0]), SnapToTileCenter_(source[1])};
        }

        [[nodiscard]] static float SnapToTileCenter_(const float value)
        {
            return std::floor(value / 16.0f) * 16.0f + 8.0f;
        }

        [[nodiscard]] static uint32_t NormalizeOverlayIdForHook_(const uint32_t overlayId)
        {
            return overlayId & 0x7FFFFFFFu;
        }

        [[nodiscard]] static float ClampOpacity_(const float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        [[nodiscard]] static int32_t RotationTurnsToPropOrientation_(const float turns)
        {
            const int32_t quarterTurns = static_cast<int32_t>(std::lround(turns * 4.0f));
            const int32_t normalized = quarterTurns % 4;
            return normalized < 0 ? normalized + 4 : normalized;
        }

        void HandleCityChange_()
        {
            cISC4City* const currentCity = director_ ? director_->GetCity() : nullptr;
            if (currentCity == observedCity_) {
                return;
            }

            ClearTransientHistory_();
            observedCity_ = currentCity;

            if (currentCity) {
                status_ = "City changed; transient histories cleared";
            }
        }

        void ClearTransientHistory_()
        {
            createdOverlays_.clear();
            selectedCreatedOverlayIndex_ = -1;
            selectedRuntimeDecalId_ = 0;
            selectedTargetKind_ = SelectedTargetKind::Overlay;
        }

        [[nodiscard]] std::optional<RuntimeDecalSnapshot> FindSelectedRuntimeDecal_(
            const std::vector<RuntimeDecalSnapshot>& runtimeDecals) const
        {
            if (selectedRuntimeDecalId_ == 0) {
                return std::nullopt;
            }

            for (const RuntimeDecalSnapshot& entry : runtimeDecals) {
                if (entry.id == selectedRuntimeDecalId_) {
                    return entry;
                }
            }

            return std::nullopt;
        }

        void CopyRuntimeDecalToEditor_(const RuntimeDecalSnapshot& entry)
        {
            textureGroup_ = entry.textureKey.group;
            textureInstance_ = entry.textureKey.instance;
            baseSize_ = entry.overlayInfo.baseSize;
            rotationTurns_ = entry.overlayInfo.rotationTurns;
            alpha_ = entry.opacity;
            enabled_ = entry.enabled;
            center_[0] = entry.worldPos.x;
            center_[1] = entry.worldPos.z;
            moveCenter_[0] = entry.worldPos.x;
            moveCenter_[1] = entry.worldPos.z;
            FetchRuntimeDecalInfo_(entry);
            FetchRuntimeDecalHookUv_(entry);
            status_ = "Selected decal prop copied to editor";
        }

        void FetchRuntimeDecalInfo_(const RuntimeDecalSnapshot& entry)
        {
            infoCenter_[0] = entry.worldPos.x;
            infoCenter_[1] = entry.worldPos.z;
            infoBaseSize_ = entry.overlayInfo.baseSize;
            infoRotationTurns_ = entry.overlayInfo.rotationTurns;
            infoAspectMultiplier_ = entry.overlayInfo.aspectMultiplier;
            infoUvScaleU_ = entry.overlayInfo.uvScaleU;
            infoUvScaleV_ = entry.overlayInfo.uvScaleV;
            infoUvOffset_ = entry.overlayInfo.uvOffset;
            infoUnknown8_ = entry.overlayInfo.unknown8;
            status_ = "Runtime decal info loaded";
        }

        void FetchRuntimeDecalHookUv_(const RuntimeDecalSnapshot& entry)
        {
            if (entry.uvSubrect) {
                hookUvSubrect_ = TerrainDecal::OverlayUvSubrect{
                    .u1 = entry.uvSubrect->u1,
                    .v1 = entry.uvSubrect->v1,
                    .u2 = entry.uvSubrect->u2,
                    .v2 = entry.uvSubrect->v2,
                    .mode = entry.uvSubrect->mode == PlopAndPaint::Sidecar::UvMode::ClipSubrect
                                ? TerrainDecal::OverlayUvMode::ClipSubrect
                                : TerrainDecal::OverlayUvMode::StretchSubrect,
                };
                hookUvMode_ = static_cast<int>(hookUvSubrect_.mode);
                hookUvOverrideEnabled_ = true;
                status_ = "Runtime decal hook UV fetched";
            }
            else {
                hookUvSubrect_ = {};
                hookUvMode_ = 0;
                hookUvOverrideEnabled_ = false;
                status_ = "Runtime decal has no hook UV override";
            }
        }

        [[nodiscard]] bool CommitRuntimeDecalSnapshot_(PlopAndPaint::Sidecar::SidecarSaveHook* sidecar,
                                                       RuntimeDecalSnapshot snapshot)
        {
            cISC4City* const city = director_ ? director_->GetCity() : nullptr;
            if (!sidecar || !city) {
                return false;
            }

            cISTETerrain* const terrain = city->GetTerrain();
            snapshot.worldPos.y = terrain ? terrain->GetAltitude(snapshot.worldPos.x, snapshot.worldPos.z) : 0.0f;
            return sidecar->UpdateRuntimeDecal(snapshot.id, city, snapshot);
        }

        bool ApplySelectedRuntimeDecalFromCreateControls_(PlopAndPaint::Sidecar::SidecarSaveHook* sidecar)
        {
            const auto selected = sidecar ? sidecar->FindRuntimeDecal(selectedRuntimeDecalId_) : std::nullopt;
            if (!selected) {
                status_ = "Apply Selected Prop failed: no selected prop";
                return false;
            }

            RuntimeDecalSnapshot snapshot = *selected;
            const auto snappedCenter = GetSnappedCenter_(center_);
            snapshot.worldPos.x = snappedCenter[0];
            snapshot.worldPos.z = snappedCenter[1];
            snapshot.textureKey.type = textureType_;
            snapshot.textureKey.group = textureGroup_;
            snapshot.textureKey.instance = textureInstance_;
            snapshot.overlayInfo.baseSize = baseSize_;
            snapshot.overlayInfo.rotationTurns = rotationTurns_;
            snapshot.opacity = ClampOpacity_(alpha_);
            snapshot.enabled = enabled_;

            if (!CommitRuntimeDecalSnapshot_(sidecar, snapshot)) {
                status_ = "Apply Selected Prop failed";
                return false;
            }

            center_[0] = snappedCenter[0];
            center_[1] = snappedCenter[1];
            moveCenter_[0] = snappedCenter[0];
            moveCenter_[1] = snappedCenter[1];
            FetchRuntimeDecalInfo_(snapshot);
            status_ = "Selected decal prop updated";
            return true;
        }

        bool ApplySelectedRuntimeDecalInfo_(PlopAndPaint::Sidecar::SidecarSaveHook* sidecar)
        {
            const auto selected = sidecar ? sidecar->FindRuntimeDecal(selectedRuntimeDecalId_) : std::nullopt;
            if (!selected) {
                status_ = "Apply runtime decal info failed: no selected prop";
                return false;
            }

            RuntimeDecalSnapshot snapshot = *selected;
            const auto snappedCenter = GetSnappedCenter_(infoCenter_);
            snapshot.worldPos.x = snappedCenter[0];
            snapshot.worldPos.z = snappedCenter[1];
            snapshot.overlayInfo.baseSize = infoBaseSize_;
            snapshot.overlayInfo.rotationTurns = infoRotationTurns_;
            snapshot.overlayInfo.aspectMultiplier = infoAspectMultiplier_;
            snapshot.overlayInfo.uvScaleU = infoUvScaleU_;
            snapshot.overlayInfo.uvScaleV = infoUvScaleV_;
            snapshot.overlayInfo.uvOffset = infoUvOffset_;
            snapshot.overlayInfo.unknown8 = infoUnknown8_;

            if (!CommitRuntimeDecalSnapshot_(sidecar, snapshot)) {
                status_ = "Apply runtime decal info failed";
                return false;
            }

            infoCenter_[0] = snappedCenter[0];
            infoCenter_[1] = snappedCenter[1];
            moveCenter_[0] = snappedCenter[0];
            moveCenter_[1] = snappedCenter[1];
            status_ = "Runtime decal info applied/stored";
            return true;
        }

        bool MoveSelectedRuntimeDecal_(PlopAndPaint::Sidecar::SidecarSaveHook* sidecar,
                                       const float worldX,
                                       const float worldZ)
        {
            const auto selected = sidecar ? sidecar->FindRuntimeDecal(selectedRuntimeDecalId_) : std::nullopt;
            if (!selected) {
                status_ = "Move runtime decal failed: no selected prop";
                return false;
            }

            RuntimeDecalSnapshot snapshot = *selected;
            snapshot.worldPos.x = worldX;
            snapshot.worldPos.z = worldZ;
            if (!CommitRuntimeDecalSnapshot_(sidecar, snapshot)) {
                status_ = "Move runtime decal failed";
                return false;
            }

            center_[0] = worldX;
            center_[1] = worldZ;
            infoCenter_[0] = worldX;
            infoCenter_[1] = worldZ;
            status_ = "Runtime decal moved";
            return true;
        }

        bool SetSelectedRuntimeDecalAlpha_(PlopAndPaint::Sidecar::SidecarSaveHook* sidecar)
        {
            const auto selected = sidecar ? sidecar->FindRuntimeDecal(selectedRuntimeDecalId_) : std::nullopt;
            if (!selected) {
                status_ = "Set runtime decal alpha failed: no selected prop";
                return false;
            }

            RuntimeDecalSnapshot snapshot = *selected;
            snapshot.opacity = ClampOpacity_(alpha_);
            if (!CommitRuntimeDecalSnapshot_(sidecar, snapshot)) {
                status_ = "Set runtime decal alpha failed";
                return false;
            }

            status_ = "Runtime decal alpha applied";
            return true;
        }

        bool SetSelectedRuntimeDecalEnabled_(PlopAndPaint::Sidecar::SidecarSaveHook* sidecar)
        {
            const auto selected = sidecar ? sidecar->FindRuntimeDecal(selectedRuntimeDecalId_) : std::nullopt;
            if (!selected) {
                status_ = "Set runtime decal enabled failed: no selected prop";
                return false;
            }

            RuntimeDecalSnapshot snapshot = *selected;
            snapshot.enabled = enabled_;
            if (!CommitRuntimeDecalSnapshot_(sidecar, snapshot)) {
                status_ = "Set runtime decal enabled failed";
                return false;
            }

            status_ = "Runtime decal visibility applied";
            return true;
        }

        bool ApplySelectedRuntimeDecalHookUv_(PlopAndPaint::Sidecar::SidecarSaveHook* sidecar)
        {
            const auto selected = sidecar ? sidecar->FindRuntimeDecal(selectedRuntimeDecalId_) : std::nullopt;
            if (!selected) {
                status_ = "Apply runtime decal hook UV failed: no selected prop";
                return false;
            }

            RuntimeDecalSnapshot snapshot = *selected;
            if (hookUvOverrideEnabled_) {
                snapshot.uvSubrect = PlopAndPaint::Sidecar::UvSubrect{
                    .u1 = hookUvSubrect_.u1,
                    .v1 = hookUvSubrect_.v1,
                    .u2 = hookUvSubrect_.u2,
                    .v2 = hookUvSubrect_.v2,
                    .mode = hookUvMode_ == 1
                                ? PlopAndPaint::Sidecar::UvMode::ClipSubrect
                                : PlopAndPaint::Sidecar::UvMode::StretchSubrect,
                };
            }
            else {
                snapshot.uvSubrect.reset();
            }

            if (!CommitRuntimeDecalSnapshot_(sidecar, snapshot)) {
                status_ = "Apply runtime decal hook UV failed";
                return false;
            }

            status_ = hookUvOverrideEnabled_ ? "Runtime decal hook UV stored" : "Runtime decal hook UV removed";
            return true;
        }

        bool RemoveSelectedRuntimeDecal_(PlopAndPaint::Sidecar::SidecarSaveHook* sidecar)
        {
            cISC4City* const city = director_ ? director_->GetCity() : nullptr;
            if (!sidecar || !city || selectedRuntimeDecalId_ == 0) {
                status_ = "Remove Selected Prop failed: no selected prop";
                return false;
            }

            if (!sidecar->RemoveRuntimeDecal(selectedRuntimeDecalId_, city)) {
                status_ = "Remove Selected Prop failed";
                return false;
            }

            selectedRuntimeDecalId_ = 0;
            selectedTargetKind_ = SelectedTargetKind::Overlay;
            status_ = "Selected decal prop removed";
            return true;
        }

        bool AddDecalProp_(const float worldX, const float worldZ)
        {
            cISC4City* const city = director_ ? director_->GetCity() : nullptr;
            if (!city) {
                status_ = "AddDecalProp failed: no city";
                return false;
            }

            cISC4OccupantManager* const occupantMgr = city->GetOccupantManager();
            if (!occupantMgr) {
                status_ = "AddDecalProp failed: no occupant manager";
                return false;
            }

            cIGZFrameWork* const framework = RZGetFrameWork();
            cIGZCOM* const com = framework ? framework->GetCOMObject() : nullptr;
            if (!com) {
                status_ = "AddDecalProp failed: no cIGZCOM";
                return false;
            }

            cRZAutoRefCount<cISC4PropOccupantTerrainDecal> decal;
            if (!com->GetClassObject(GZCLSID_cISC4PropOccupantTerrainDecal,
                                     GZIID_cISC4PropOccupantTerrainDecal,
                                     decal.AsPPVoid())
                || !decal) {
                status_ = "AddDecalProp failed: GetClassObject";
                LOG_WARN("OverlayManagerDebugPanel: GetClassObject for terrain decal prop failed");
                return false;
            }

            cRZAutoRefCount<cISC4Occupant> occupant;
            cRZAutoRefCount<cISC4PropOccupant> propOccupant;
            if (decal->QueryInterface(GZIID_cISC4PropOccupant, propOccupant.AsPPVoid())
                && propOccupant) {
                occupant = cRZAutoRefCount<cISC4Occupant>(propOccupant->AsOccupant(),
                                                          cRZAutoRefCount<cISC4Occupant>::kAddRef);
            }
            if (!occupant) {
                if (!decal->QueryInterface(GZIID_cISC4Occupant, occupant.AsPPVoid())
                    || !occupant) {
                    status_ = "AddDecalProp failed: QI cISC4Occupant";
                    LOG_WARN("OverlayManagerDebugPanel: terrain decal prop has no cISC4Occupant facet");
                    return false;
                }
            }

            const cGZPersistResourceKey textureKey(textureType_, textureGroup_, textureInstance_);
            decal->SetDecalTexture(textureKey, 1.0f);
            decal->SetDecalSize(baseSize_);
            decal->SetOpacity(ClampOpacity_(alpha_));

            if (propOccupant) {
                const int32_t orientation = RotationTurnsToPropOrientation_(rotationTurns_);
                if (!propOccupant->SetOrientation(orientation)) {
                    LOG_WARN("OverlayManagerDebugPanel: initial SetOrientation failed for decal prop");
                }
            }

            if (!occupant->IsInitialized()) {
                occupant->Init();
            }

            cISTETerrain* const terrain = city->GetTerrain();
            const float worldY = terrain ? terrain->GetAltitude(worldX, worldZ) : 0.0f;
            const cS3DVector3 pos(worldX, worldY, worldZ);
            if (!occupant->SetPosition(&pos)) {
                status_ = "AddDecalProp failed: SetPosition";
                return false;
            }

            if (!occupantMgr->InsertOccupant(occupant, 0)) {
                status_ = "AddDecalProp failed: InsertOccupant";
                LOG_WARN("OverlayManagerDebugPanel: InsertOccupant returned false at ({:.2f}, {:.2f}, {:.2f})",
                         pos.fX, pos.fY, pos.fZ);
                return false;
            }

            LOG_INFO("OverlayManagerDebugPanel: placed decal prop at ({:.2f}, {:.2f}, {:.2f}) tex=({:08X},{:08X},{:08X}) size={:.2f}",
                     pos.fX, pos.fY, pos.fZ,
                     textureType_, textureGroup_, textureInstance_,
                     baseSize_);
            status_ = "Decal prop inserted";

            if (PlopAndPaint::Sidecar::SidecarSaveHook* sidecar = director_ ? director_->GetSidecarSaveHook() : nullptr) {
                RuntimeDecalSnapshot seedSnapshot{};
                seedSnapshot.worldPos = {pos.fX, pos.fY, pos.fZ};
                seedSnapshot.textureKey = {textureType_, textureGroup_, textureInstance_};
                seedSnapshot.overlayInfo.baseSize = baseSize_;
                seedSnapshot.overlayInfo.rotationTurns = rotationTurns_;
                seedSnapshot.opacity = ClampOpacity_(alpha_);
                seedSnapshot.enabled = true;

                if (const auto trackedId = sidecar->TrackRuntimeDecal(occupant, &seedSnapshot)) {
                    selectedRuntimeDecalId_ = *trackedId;
                    selectedTargetKind_ = SelectedTargetKind::Prop;
                    if (const auto snapshot = sidecar->FindRuntimeDecal(*trackedId)) {
                        CopyRuntimeDecalToEditor_(*snapshot);
                    }
                }
            }
            return true;
        }

        void PushCreatedOverlay_(const uint32_t overlayId, const bool ring)
        {
            createdOverlays_.insert(createdOverlays_.begin(),
                                    CreatedOverlayEntry{
                                        .overlayId = overlayId,
                                        .textureGroup = textureGroup_,
                                        .textureInstance = textureInstance_,
                                        .ring = ring,
                                    });
            constexpr size_t kMaxCreatedOverlayHistory = 32;
            if (createdOverlays_.size() > kMaxCreatedOverlayHistory) {
                createdOverlays_.resize(kMaxCreatedOverlayHistory);
            }
            selectedCreatedOverlayIndex_ = 0;
        }

    private:
        SC4PlopAndPaintDirector* director_ = nullptr;
        int overlayType_ = static_cast<int>(cISTETerrainView::tOverlayManagerType::DynamicLand);
        static constexpr uint32_t textureType_ = 0x7AB50E44;
        uint32_t textureGroup_ = 0x1ABE787D;
        uint32_t textureInstance_ = 0xAA40173A;
        uint32_t overlayId_ = 0;
        uint32_t lastOverlayId_ = 0;
        std::vector<CreatedOverlayEntry> createdOverlays_{};
        int selectedCreatedOverlayIndex_ = -1;
        cISC4City* observedCity_ = nullptr;
        uint64_t selectedRuntimeDecalId_ = 0;
        SelectedTargetKind selectedTargetKind_ = SelectedTargetKind::Overlay;
        float center_[2] = {512.0f, 512.0f};
        float moveCenter_[2] = {512.0f, 512.0f};
        float baseSize_ = 16.0f;
        float rotationTurns_ = 0.0f;
        float alpha_ = 1.0f;
        bool enabled_ = true;
        bool snapCenterToTile_ = true;
        bool applyCenterViaMoveDecal_ = true;
        float infoCenter_[2] = {512.0f, 512.0f};
        float infoBaseSize_ = 16.0f;
        float infoRotationTurns_ = 0.0f;
        float infoAspectMultiplier_ = 1.0f;
        float infoUvScaleU_ = 1.0f;
        float infoUvScaleV_ = 1.0f;
        float infoUvOffset_ = 0.0f;
        float infoUnknown8_ = 0.0f;
        bool hookUvOverrideEnabled_ = false;
        int hookUvMode_ = 0;
        TerrainDecal::OverlayUvSubrect hookUvSubrect_{};
        const char* status_ = "Idle";

        static constexpr const char* kOverlayTypeNames[] = {
            "StaticLand",
            "StaticWater",
            "DynamicLand",
            "DynamicWater"
        };
        static constexpr const char* kHookUvModeNames[] = {
            "Stretch",
            "Clip Only"
        };
    };

    OverlayManagerDebugPanel::OverlayManagerDebugPanel(SC4PlopAndPaintDirector* director)
        : director_(director)
    {
    }

    OverlayManagerDebugPanel::~OverlayManagerDebugPanel() = default;

    void OverlayManagerDebugPanel::OnInit()
    {
        state_ = std::make_unique<OverlayManagerDebugPanelState>(director_);
        state_->OnInit();
    }

    void OverlayManagerDebugPanel::OnRender()
    {
        if (state_) {
            state_->OnRender();
        }
    }

    void OverlayManagerDebugPanel::OnShutdown()
    {
        if (state_) {
            state_->OnShutdown();
        }
    }
}
