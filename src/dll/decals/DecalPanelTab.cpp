#include "DecalPanelTab.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

#include "cGZPersistResourceKey.h"
#include "cIGZPersistDBSegment.h"
#include "cIGZPersistResourceManager.h"
#include "cRZAutoRefCount.h"
#include "cISTETerrainView.h"
#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/Constants.hpp"
#include "../utils/Logger.h"
#include "FSHReader.h"

namespace {
    constexpr float kIconSize = 64.0f;
    constexpr float kActionButtonWidth = 120.0f;
    constexpr uint32_t kDecalTextureType  = 0x7AB50E44;
    constexpr uint32_t kDecalTextureGroup = 0x0986135E;

    std::string FormatInstanceId(const uint32_t id) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08X", id);
        return buf;
    }

    cGZPersistResourceKey MakeDecalTextureKey(const uint32_t instanceId) {
        return cGZPersistResourceKey{kDecalTextureType, kDecalTextureGroup, instanceId};
    }
}

DecalPanelTab::DecalPanelTab(SC4PlopAndPaintDirector* director,
                              DecalRepository* decals,
                              cIGZPersistResourceManager* pRM,
                              cIGZImGuiService* imguiService)
    : PanelTab(director, nullptr, nullptr, nullptr, imguiService)
    , decals_(decals)
    , pRM_(pRM) {
    pendingPaint_.settings.stateTemplate.decalInfo.baseSize = 16.0f;
    pendingPaint_.settings.stateTemplate.opacity            = 1.0f;
    pendingPaint_.settings.stateTemplate.overlayType        =
        cISTETerrainView::tOverlayManagerType::DynamicLand;
}

void DecalPanelTab::OnRender() {
    if (!decals_) {
        ImGui::TextUnformatted("Decal repository not available.");
        return;
    }

    if (!director_->IsDecalServiceAvailable()) {
        ImGui::TextWrapped("Terrain decal service not available. Ensure you are running SC4 1.1.641 "
                           "with EnableTerrainDecalService=true in SC4PlopAndPaint.ini.");
        return;
    }

    if (decals_->Count() == 0) {
        ImGui::TextUnformatted("No decal textures found (type 0x7AB50E44, group 0x0986135E).");
        return;
    }

    RenderFilterBar_();
    ImGui::Separator();

    std::vector<size_t> filtered;
    BuildFilteredIndices_(filtered);
    const bool hasSelection = selectedInstanceId_ != 0;

    ImGui::Text("Showing %zu of %zu textures", filtered.size(), decals_->Count());
    if (director_->IsDecalPainting()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopDecalPainting();
        }
    }

    // Decal strip controls
    if (director_->IsDecalStripping()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Stop stripping")) {
            director_->StopDecalStripping();
        }
    }
    else {
        ImGui::SameLine();
        if (ImGui::SmallButton("Strip decals")) {
            ReleaseImGuiInputCapture_();
            director_->StartDecalStripping();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Click decals to remove them.\nPress B for brush mode.\nCtrl+Z to undo.\nESC to stop.");
        }
    }

    if (!hasSelection) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Paint selected", ImVec2(kActionButtonWidth, 0.0f))) {
        QueuePaintForDecal_(selectedInstanceId_);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open the paint settings for the selected decal.\nYou can also press Enter after selecting a texture.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Paint now", ImVec2(kActionButtonWidth, 0.0f))) {
        StartPaintingDecal_(selectedInstanceId_);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Start painting immediately with the current decal settings.");
    }
    if (!hasSelection) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (hasSelection) {
        ImGui::TextDisabled("Selected: 0x%08X", selectedInstanceId_);
    }
    else {
        ImGui::TextDisabled("Click a texture to select it.");
    }

    if (hasSelection &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
        QueuePaintForDecal_(selectedInstanceId_);
    }

    ImGui::Separator();
    RenderDecalGrid_(filtered);
    RenderSettingsModal_();
}

void DecalPanelTab::OnDeviceReset(const uint32_t deviceGeneration) {
    if (deviceGeneration != lastDeviceGeneration_) {
        thumbnailCache_.Clear();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

void DecalPanelTab::RenderFilterBar_() {
    ImGui::TextUnformatted("Filters");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(UI::iidFilterWidth());
    ImGui::InputTextWithHint("##DecalIID", "IID prefix...", iidFilterBuf_, sizeof(iidFilterBuf_));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Filter by hex instance ID prefix (e.g. '25A7')");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear filters")) {
        iidFilterBuf_[0] = '\0';
    }
}

void DecalPanelTab::BuildFilteredIndices_(std::vector<size_t>& out) const {
    const auto& ids = decals_->GetInstanceIds();
    out.reserve(ids.size());

    const bool hasFilter = iidFilterBuf_[0] != '\0';

    for (size_t i = 0; i < ids.size(); ++i) {
        if (hasFilter) {
            const std::string hex = FormatInstanceId(ids[i]);
            // case-insensitive prefix match
            const size_t filterLen = std::strlen(iidFilterBuf_);
            if (hex.size() < filterLen) {
                continue;
            }
            bool match = true;
            for (size_t c = 0; c < filterLen; ++c) {
                if (std::toupper(static_cast<unsigned char>(hex[c])) !=
                    std::toupper(static_cast<unsigned char>(iidFilterBuf_[c]))) {
                    match = false;
                    break;
                }
            }
            if (!match) {
                continue;
            }
        }
        out.push_back(i);
    }
}

void DecalPanelTab::RenderDecalGrid_(const std::vector<size_t>& indices) {
    const auto& ids = decals_->GetInstanceIds();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 btnSize{kIconSize, kIconSize};
    const float tileWidth = btnSize.x + style.FramePadding.x * 2.0f;
    const float rowStride = tileWidth + style.ItemSpacing.x;

    if (!ImGui::BeginChild("DecalGrid", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::EndChild();
        return;
    }

    if (indices.empty()) {
        ImGui::TextDisabled("No decals match the current filters.");
        ImGui::EndChild();
        return;
    }

    const float availWidth = ImGui::GetContentRegionAvail().x;
    const auto columns = std::max(1, static_cast<int>((availWidth + style.ItemSpacing.x) / rowStride));

    ImGuiListClipper clipper;
    const auto rowCount = static_cast<int>((indices.size() + columns - 1) / columns);
    clipper.Begin(rowCount, btnSize.y + style.FramePadding.y * 2.0f + style.ItemSpacing.y);

    while (clipper.Step()) {
        for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (auto col = 0; col < columns; ++col) {
                const int idx = row * columns + col;
                if (static_cast<size_t>(idx) >= indices.size()) {
                    break;
                }

                const uint32_t instanceId = ids[indices[static_cast<size_t>(idx)]];

                if (col > 0) {
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
                }

                const bool selected = (instanceId == selectedInstanceId_);

                // Try to get cached thumbnail
                const auto texOpt = thumbnailCache_.Get(instanceId);

                ImGui::PushID(static_cast<int>(instanceId));
                bool clicked = false;
                bool dblClicked = false;

                if (texOpt.has_value() && *texOpt != nullptr) {
                    ImGui::ImageButton("##decal",
                        reinterpret_cast<ImTextureID>(*texOpt), btnSize);
                }
                else {
                    char label[12];
                    std::snprintf(label, sizeof(label), "##d%08X", instanceId);
                    ImGui::Button(label, btnSize);
                    thumbnailCache_.Request(instanceId);
                }
                clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                dblClicked = clicked && ImGui::GetMouseClickedCount(ImGuiMouseButton_Left) >= 2;

                if (selected) {
                    const ImVec2 rectMin = ImGui::GetItemRectMin();
                    const ImVec2 rectMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRect(
                        ImVec2(rectMin.x - 2.0f, rectMin.y - 2.0f),
                        ImVec2(rectMax.x + 2.0f, rectMax.y + 2.0f),
                        IM_COL32(255, 200, 0, 255), 2.0f);
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "0x%08X\nClick to select.\nDouble-click or press Enter to open paint settings.\nUse Paint now to skip the popup.",
                        instanceId);
                }

                if (dblClicked) {
                    selectedInstanceId_ = instanceId;
                    QueuePaintForDecal_(instanceId);
                }
                else if (clicked) {
                    selectedInstanceId_ = instanceId;
                }

                ImGui::PopID();
            }
        }
    }
    clipper.End();

    thumbnailCache_.ProcessLoadQueue([this](const uint32_t instanceId) {
        return LoadDecalThumbnail_(instanceId);
    });

    ImGui::EndChild();
}

void DecalPanelTab::RenderSettingsModal_() {
    if (pendingPaint_.open) {
        ImGui::OpenPopup("Decal Paint Settings");
        pendingPaint_.open = false;
    }

    if (!ImGui::BeginPopupModal("Decal Paint Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    TerrainDecalState& state = pendingPaint_.settings.stateTemplate;

    ImGui::DragFloat("Base size (m)##decalSize", &state.decalInfo.baseSize, 1.0f, 0.5f, 512.0f);
    ImGui::DragFloat("Rotation (turns)##decalRot", &state.decalInfo.rotationTurns, 0.01f, 0.0f, 1.0f);
    ImGui::SliderFloat("Opacity##decalOpacity", &state.opacity, 0.0f, 1.0f);
    ImGui::ColorEdit3("Color tint##decalColor", &state.color.fX);

    const char* overlayTypes[] = {"StaticLand", "StaticWater", "DynamicLand", "DynamicWater"};
    int overlayIdx = static_cast<int>(state.overlayType);
    if (ImGui::Combo("Overlay type##decalOverlay", &overlayIdx, overlayTypes, 4)) {
        state.overlayType = static_cast<cISTETerrainView::tOverlayManagerType>(overlayIdx);
    }

    ImGui::DragFloat("Aspect multiplier##decalAspect",
                     &state.decalInfo.aspectMultiplier, 0.01f, 0.1f, 10.0f);

    ImGui::Separator();

    if (ImGui::Button("Start Painting", ImVec2(120, 0))) {
        char name[20];
        std::snprintf(name, sizeof(name), "0x%08X", pendingPaint_.instanceId);
        ReleaseImGuiInputCapture_();
        director_->StartDecalPainting(pendingPaint_.instanceId, pendingPaint_.settings, name);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void DecalPanelTab::QueuePaintForDecal_(const uint32_t instanceId) {
    pendingPaint_.instanceId = instanceId;
    // Preserve settings but update texture key
    pendingPaint_.settings.stateTemplate.textureKey = MakeDecalTextureKey(instanceId);
    pendingPaint_.open = true;
}

void DecalPanelTab::StartPaintingDecal_(const uint32_t instanceId) {
    if (instanceId == 0) {
        return;
    }

    pendingPaint_.instanceId = instanceId;
    pendingPaint_.settings.stateTemplate.textureKey = MakeDecalTextureKey(instanceId);

    char name[20];
    std::snprintf(name, sizeof(name), "0x%08X", instanceId);

    ReleaseImGuiInputCapture_();
    director_->StartDecalPainting(instanceId, pendingPaint_.settings, name);
}

ImGuiTexture DecalPanelTab::LoadDecalThumbnail_(const uint32_t instanceId) const {
    ImGuiTexture texture;

    if (!pRM_ || !imguiService_) {
        return texture;
    }

    const cGZPersistResourceKey key = MakeDecalTextureKey(instanceId);

    // Find the DB segment containing this key and read raw bytes
    cRZAutoRefCount<cIGZPersistDBSegment> segment;
    if (!pRM_->FindDBSegment(key, segment.AsPPObj()) || !segment) {
        LOG_DEBUG("DecalPanelTab: no segment for decal 0x{:08X}", instanceId);
        return texture;
    }

    const uint32_t size = segment->GetRecordSize(key);
    if (size == 0) {
        LOG_WARN("DecalPanelTab: zero-size record for decal 0x{:08X}", instanceId);
        return texture;
    }

    std::vector<uint8_t> buf(size);
    uint32_t readSize = size;
    if (segment->ReadRecord(key, buf.data(), readSize) == 0) {
        LOG_WARN("DecalPanelTab: failed to read record for decal 0x{:08X}", instanceId);
        return texture;
    }

    // Parse FSH record
    auto parseResult = FSH::Reader::Parse(std::span<const uint8_t>(buf.data(), readSize));
    if (!parseResult) {
        LOG_WARN("DecalPanelTab: FSH parse failed for decal 0x{:08X}", instanceId);
        return texture;
    }

    const FSH::Record& record = *parseResult;
    if (record.entries.empty() || record.entries[0].bitmaps.empty()) {
        LOG_WARN("DecalPanelTab: FSH has no bitmaps for decal 0x{:08X}", instanceId);
        return texture;
    }

    // Use the first (highest-resolution) bitmap from the first entry
    const FSH::Bitmap& bitmap = record.entries[0].bitmaps[0];

    std::vector<uint8_t> rgba;
    if (!FSH::Reader::ConvertToRGBA8(bitmap, rgba)) {
        LOG_WARN("DecalPanelTab: RGBA conversion failed for decal 0x{:08X}", instanceId);
        return texture;
    }

    // ImGuiTexture::Create expects BGRA8 — swap R and B channels
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        std::swap(rgba[i], rgba[i + 2]);
    }

    texture.Create(imguiService_, bitmap.width, bitmap.height, rgba.data());
    return texture;
}
