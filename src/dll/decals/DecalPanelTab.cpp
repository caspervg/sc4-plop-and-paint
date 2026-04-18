#include "DecalPanelTab.hpp"

#include <algorithm>
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
    constexpr int   kColumns  = 6;

    std::string FormatInstanceId(const uint32_t id) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08X", id);
        return buf;
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
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("IID prefix##DecalIID", iidFilterBuf_, sizeof(iidFilterBuf_));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Filter by hex instance ID prefix (e.g. '25A7')");
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
    const float cellSize = kIconSize + ImGui::GetStyle().ItemSpacing.x;

    if (!ImGui::BeginChild("DecalGrid", ImVec2(0, 0), false)) {
        ImGui::EndChild();
        return;
    }

    const float availWidth = ImGui::GetContentRegionAvail().x;
    const auto columns = std::max(1, static_cast<int>(availWidth / cellSize));

    ImGuiListClipper clipper;
    const auto rowCount = static_cast<int>((indices.size() + columns - 1) / columns);
    clipper.Begin(rowCount, kIconSize + ImGui::GetStyle().ItemSpacing.y);

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

                // Draw border for selected cell
                if (selected) {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRect(
                        ImVec2(pos.x - 2, pos.y - 2),
                        ImVec2(pos.x + kIconSize + 2, pos.y + kIconSize + 2),
                        IM_COL32(255, 200, 0, 255), 2.0f);
                }

                // Try to get cached thumbnail
                const auto texOpt = thumbnailCache_.Get(instanceId);

                ImGui::PushID(static_cast<int>(instanceId));
                ImVec2 btnSize{kIconSize, kIconSize};

                auto clicked = false;
                auto dblClicked = false;

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
                clicked    = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                dblClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("0x%08X\nDouble-click to paint", instanceId);
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
    if (!pendingPaint_.open) {
        return;
    }

    ImGui::OpenPopup("Decal Paint Settings");
    pendingPaint_.open = false;

    if (ImGui::BeginPopupModal("Decal Paint Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        TerrainDecalState& state = pendingPaint_.settings.stateTemplate;

        ImGui::DragFloat("Base size (m)##decalSize", &state.decalInfo.baseSize, 1.0f, 0.5f, 512.0f);
        ImGui::DragFloat("Rotation (turns)##decalRot", &state.decalInfo.rotationTurns, 0.01f, 0.0f, 1.0f);
        ImGui::SliderFloat("Opacity##decalOpacity", &state.opacity, 0.0f, 1.0f);
        ImGui::ColorEdit3("Color tint##decalColor", reinterpret_cast<float*>(&state.color));

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
}

void DecalPanelTab::QueuePaintForDecal_(const uint32_t instanceId) {
    pendingPaint_.instanceId = instanceId;
    // Preserve settings but update texture key
    pendingPaint_.settings.stateTemplate.textureKey = cGZPersistResourceKey{0x7AB50E44, 0x0986135E, instanceId};
    pendingPaint_.open = true;
}

ImGuiTexture DecalPanelTab::LoadDecalThumbnail_(const uint32_t instanceId) const {
    ImGuiTexture texture;

    if (!pRM_ || !imguiService_) {
        return texture;
    }

    const cGZPersistResourceKey key{0x7AB50E44, 0x0986135E, instanceId};

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
