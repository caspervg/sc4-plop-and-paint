#include "OverlayManagerDebugPanel.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "SC4PlopAndPaintDirector.hpp"
#include "cGZPersistResourceKey.h"
#include "cISC4City.h"
#include "cISTEOverlayManager.h"
#include "cISTETerrain.h"
#include "cISTETerrainView.h"
#include "cS3DVector2.h"
#include "imgui.h"
#include "terrain/TerrainDecalHook.hpp"
#include "utils/Logger.h"

namespace DebugUi
{
    class OverlayManagerDebugPanelState final
    {
    public:
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

        void OnInit()
        {
            LOG_INFO("OverlayManagerDebugPanel: initialized");
        }

        void OnRender()
        {
            cISTEOverlayManager* overlay = ResolveOverlayManager_();
            TerrainDecal::TerrainDecalHook* hook = director_ ? director_->GetTerrainDecalHook() : nullptr;
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

            ImGui::SeparatorText("Overlay");
            ImGui::InputScalar("Overlay ID", ImGuiDataType_U32, &overlayId_, nullptr, nullptr, "%u");
            ImGui::SameLine();
            if (ImGui::Button("Use Last Created")) {
                overlayId_ = lastOverlayId_;
            }
            ImGui::Text("Hook UV key: %u (0x%08X)", hookOverlayKey, hookOverlayKey);

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
                    status_ = "Selected overlay restored from history";
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("Clear Overlay History")) {
                createdOverlays_.clear();
                selectedCreatedOverlayIndex_ = -1;
                status_ = "Overlay history cleared";
            }

            if (!overlayAvailable) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Fetch DecalInfo")) {
                FetchDecalInfo_(overlay);
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Overlay")) {
                overlay->RemoveOverlay(overlayId_);
                if (hook) {
                    hook->RemoveOverlayUvSubrect(overlayId_);
                }
                status_ = "RemoveOverlay called";
            }

            ImGui::InputFloat2("Move to center (x,z)", &moveCenter_[0]);
            if (ImGui::Button("Move Decal")) {
                const auto snappedCenter = GetSnappedCenter_(moveCenter_);
                overlay->MoveDecal(overlayId_, cS3DVector2{snappedCenter[0], snappedCenter[1]});
                moveCenter_[0] = snappedCenter[0];
                moveCenter_[1] = snappedCenter[1];
                status_ = "MoveDecal called";
            }
            ImGui::InputFloat("Alpha", &alpha_, 0.05f, 0.5f, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("Set Alpha")) {
                overlay->SetOverlayAlpha(overlayId_, alpha_);
                status_ = "SetOverlayAlpha called";
            }
            ImGui::Checkbox("Enabled", &enabled_);
            ImGui::SameLine();
            if (ImGui::Button("Apply Enabled")) {
                overlay->SetOverlayEnabled(overlayId_, enabled_);
                status_ = "SetOverlayEnabled called";
            }
            if (!overlayAvailable) {
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

            if (!overlayAvailable) {
                ImGui::BeginDisabled();
            }
            if (overlayAvailable && applyDecalInfoNow) {
                ApplyDecalInfo_(overlay);
            }
            if (!overlayAvailable) {
                ImGui::EndDisabled();
            }

            ImGui::SeparatorText("Hook UV Subrect");
            if (!hook) {
                ImGui::TextDisabled("Terrain decal hook unavailable.");
            }
            else {
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
                if (applyHookUvNow) {
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
                        hook->RemoveOverlayUvSubrect(overlayId_);
                        status_ = "Removed hook UV override";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply Hook UV")) {
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
                        hook->RemoveOverlayUvSubrect(overlayId_);
                        status_ = "Removed hook UV override";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Hook UV")) {
                    LOG_INFO("OverlayManagerDebugPanel: clearing hook UV override for overlay {} (hook key {})",
                             overlayId_,
                             hookOverlayKey);
                    hook->RemoveOverlayUvSubrect(overlayId_);
                    hookUvSubrect_ = {};
                    hookUvMode_ = 0;
                    hookUvOverrideEnabled_ = false;
                    status_ = "Cleared hook UV override";
                }
            }

            ImGui::Separator();
            ImGui::TextWrapped("%s", status_);
            ImGui::End();
        }

        void OnShutdown()
        {
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
