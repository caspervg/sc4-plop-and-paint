#include "DecalPanelTab.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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
    constexpr float kDegreesPerTurn = 360.0f;
    constexpr float kUvPickerMaxWidth = 320.0f;
    constexpr float kUvPickerMaxHeight = 220.0f;
    constexpr float kUvPickerMinDragPixels = 3.0f;

    std::string FormatInstanceId(const uint32_t id) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08X", id);
        return buf;
    }

    cGZPersistResourceKey MakeDecalTextureKey(const uint32_t instanceId) {
        return cGZPersistResourceKey{kDecalTextureType, kDecalTextureGroup, instanceId};
    }

    float NormalizeTurns(const float turns) {
        float normalizedTurns = std::fmod(turns, 1.0f);
        if (normalizedTurns < 0.0f) {
            normalizedTurns += 1.0f;
        }
        return normalizedTurns;
    }

    float TurnsToDegrees(const float turns) {
        return NormalizeTurns(turns) * kDegreesPerTurn;
    }

    float DegreesToTurns(const float degrees) {
        return NormalizeTurns(degrees / kDegreesPerTurn);
    }

    ImVec2 ClampUvPoint(const ImVec2 uv) {
        return ImVec2(std::clamp(uv.x, 0.0f, 1.0f), std::clamp(uv.y, 0.0f, 1.0f));
    }

    float QuantizeUvEdge(const float uv, const float pixelCount) {
        if (pixelCount <= 0.0f) {
            return std::clamp(uv, 0.0f, 1.0f);
        }

        const float clamped = std::clamp(uv, 0.0f, 1.0f);
        return std::clamp(std::round(clamped * pixelCount) / pixelCount, 0.0f, 1.0f);
    }

    void NormalizeUvWindow(TerrainDecalUvWindow& window) {
        window.u1 = std::clamp(window.u1, 0.0f, 1.0f);
        window.v1 = std::clamp(window.v1, 0.0f, 1.0f);
        window.u2 = std::clamp(window.u2, 0.0f, 1.0f);
        window.v2 = std::clamp(window.v2, 0.0f, 1.0f);

        if (window.u1 > window.u2) {
            std::swap(window.u1, window.u2);
        }
        if (window.v1 > window.v2) {
            std::swap(window.v1, window.v2);
        }
    }

    void QuantizeUvWindowToPixels(TerrainDecalUvWindow& window, const ImVec2 sourceSize) {
        NormalizeUvWindow(window);

        const float pixelWidth = std::max(1.0f, sourceSize.x);
        const float pixelHeight = std::max(1.0f, sourceSize.y);
        const float minUSpan = 1.0f / pixelWidth;
        const float minVSpan = 1.0f / pixelHeight;

        window.u1 = QuantizeUvEdge(window.u1, pixelWidth);
        window.v1 = QuantizeUvEdge(window.v1, pixelHeight);
        window.u2 = QuantizeUvEdge(window.u2, pixelWidth);
        window.v2 = QuantizeUvEdge(window.v2, pixelHeight);
        NormalizeUvWindow(window);

        if (window.u2 - window.u1 < minUSpan) {
            window.u2 = std::min(1.0f, window.u1 + minUSpan);
            window.u1 = std::max(0.0f, window.u2 - minUSpan);
        }
        if (window.v2 - window.v1 < minVSpan) {
            window.v2 = std::min(1.0f, window.v1 + minVSpan);
            window.v1 = std::max(0.0f, window.v2 - minVSpan);
        }

        window.u1 = QuantizeUvEdge(window.u1, pixelWidth);
        window.v1 = QuantizeUvEdge(window.v1, pixelHeight);
        window.u2 = QuantizeUvEdge(window.u2, pixelWidth);
        window.v2 = QuantizeUvEdge(window.v2, pixelHeight);
        NormalizeUvWindow(window);
    }

    void GetUvPixelRect(const TerrainDecalUvWindow& window,
                        const ImVec2 sourceSize,
                        int& outX,
                        int& outY,
                        int& outWidth,
                        int& outHeight) {
        const int textureWidth = std::max(1, static_cast<int>(std::lround(sourceSize.x)));
        const int textureHeight = std::max(1, static_cast<int>(std::lround(sourceSize.y)));

        outX = std::clamp(static_cast<int>(std::lround(window.u1 * textureWidth)), 0, textureWidth - 1);
        outY = std::clamp(static_cast<int>(std::lround(window.v1 * textureHeight)), 0, textureHeight - 1);

        const int x2 = std::clamp(static_cast<int>(std::lround(window.u2 * textureWidth)), outX + 1, textureWidth);
        const int y2 = std::clamp(static_cast<int>(std::lround(window.v2 * textureHeight)), outY + 1, textureHeight);

        outWidth = std::max(1, x2 - outX);
        outHeight = std::max(1, y2 - outY);
    }

    void SetUvWindowFromPixelRect(TerrainDecalUvWindow& window,
                                  const ImVec2 sourceSize,
                                  int x,
                                  int y,
                                  int width,
                                  int height) {
        const int textureWidth = std::max(1, static_cast<int>(std::lround(sourceSize.x)));
        const int textureHeight = std::max(1, static_cast<int>(std::lround(sourceSize.y)));

        x = std::clamp(x, 0, textureWidth - 1);
        y = std::clamp(y, 0, textureHeight - 1);
        width = std::clamp(width, 1, textureWidth - x);
        height = std::clamp(height, 1, textureHeight - y);

        window.u1 = static_cast<float>(x) / static_cast<float>(textureWidth);
        window.v1 = static_cast<float>(y) / static_cast<float>(textureHeight);
        window.u2 = static_cast<float>(x + width) / static_cast<float>(textureWidth);
        window.v2 = static_cast<float>(y + height) / static_cast<float>(textureHeight);
        QuantizeUvWindowToPixels(window, sourceSize);
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
        ClearUvPickerTexture_();
        lastDeviceGeneration_ = deviceGeneration;
    }
}

void DecalPanelTab::OnShutdown() {
    thumbnailCache_.Clear();
    ClearUvPickerTexture_();
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
    float rotationDegrees = TurnsToDegrees(state.decalInfo.rotationTurns);

    ImGui::DragFloat("Base size (m)##decalSize", &state.decalInfo.baseSize, 1.0f, 0.5f, 512.0f);
    if (ImGui::DragFloat("Rotation (deg)##decalRot", &rotationDegrees, 1.0f, -3600.0f, 3600.0f, "%.1f")) {
        state.decalInfo.rotationTurns = DegreesToTurns(rotationDegrees);
    }
    ImGui::SliderFloat("Opacity##decalOpacity", &state.opacity, 0.0f, 1.0f);
    ImGui::ColorEdit3("Color tint##decalColor", &state.color.fX);

    const char* overlayTypes[] = {"StaticLand", "StaticWater", "DynamicLand", "DynamicWater"};
    int overlayIdx = static_cast<int>(state.overlayType);
    if (ImGui::Combo("Overlay type##decalOverlay", &overlayIdx, overlayTypes, 4)) {
        state.overlayType = static_cast<cISTETerrainView::tOverlayManagerType>(overlayIdx);
    }

    ImGui::DragFloat("Aspect multiplier##decalAspect",
                     &state.decalInfo.aspectMultiplier, 0.01f, 0.1f, 10.0f);
    ImGui::TextDisabled("In-game hotkeys: R rotates +45 deg, Shift+R rotates +5 deg.");

    ImGui::Separator();
    ImGui::TextUnformatted("UV Window");
    ImGui::Checkbox("Use UV window##decalHasUvWindow", &state.hasUvWindow);
    if (uvPickerSourceSize_.x > 0.0f && uvPickerSourceSize_.y > 0.0f) {
        ImGui::TextDisabled("Texture %.0f x %.0f px. Drag to select; bounds snap to pixels.",
                            uvPickerSourceSize_.x,
                            uvPickerSourceSize_.y);
    }
    else {
        ImGui::TextDisabled("Drag on the texture to define the UV sub-rect.");
    }
    RenderUvPicker_(state);
    ImGui::BeginDisabled(!state.hasUvWindow);
    if (ImGui::DragFloat4("UV bounds##decalUvWindow", &state.uvWindow.u1, 0.01f, 0.0f, 1.0f, "%.2f")) {
        NormalizeUvWindow(state.uvWindow);
        if (uvPickerSourceSize_.x > 0.0f && uvPickerSourceSize_.y > 0.0f) {
            QuantizeUvWindowToPixels(state.uvWindow, uvPickerSourceSize_);
        }
    }
    if (uvPickerSourceSize_.x > 0.0f && uvPickerSourceSize_.y > 0.0f) {
        int originPx[2]{};
        int sizePx[2]{};
        GetUvPixelRect(state.uvWindow, uvPickerSourceSize_, originPx[0], originPx[1], sizePx[0], sizePx[1]);

        bool pixelEdited = false;
        if (ImGui::InputInt2("Origin (px)##decalUvOrigin", originPx)) {
            pixelEdited = true;
        }
        if (ImGui::InputInt2("Size (px)##decalUvSize", sizePx)) {
            pixelEdited = true;
        }
        if (pixelEdited) {
            state.hasUvWindow = true;
            SetUvWindowFromPixelRect(state.uvWindow,
                                     uvPickerSourceSize_,
                                     originPx[0],
                                     originPx[1],
                                     sizePx[0],
                                     sizePx[1]);
        }
    }
    const char* uvModes[] = {"Stretch subrect", "Clip subrect"};
    int uvMode = static_cast<int>(state.uvWindow.mode);
    if (ImGui::Combo("UV mode##decalUvMode", &uvMode, uvModes, IM_ARRAYSIZE(uvModes))) {
        state.uvWindow.mode = static_cast<TerrainDecalUvMode>(uvMode);
    }
    if (ImGui::Button("Disable UV Window")) {
        state.hasUvWindow = false;
        state.uvWindow = {};
    }
    ImGui::EndDisabled();

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

void DecalPanelTab::RenderUvPicker_(TerrainDecalState& state) {
    if (!EnsureUvPickerTextureLoaded_()) {
        ImGui::TextDisabled("Texture preview unavailable for this decal.");
        return;
    }

    void* const textureId = uvPickerTexture_.GetID();
    if (!textureId || uvPickerSourceSize_.x <= 0.0f || uvPickerSourceSize_.y <= 0.0f) {
        ImGui::TextDisabled("Texture preview unavailable for this decal.");
        return;
    }

    const float maxWidth = std::min(kUvPickerMaxWidth, std::max(1.0f, ImGui::GetContentRegionAvail().x));
    const float widthScale = maxWidth / uvPickerSourceSize_.x;
    const float heightScale = kUvPickerMaxHeight / uvPickerSourceSize_.y;
    const float scale = std::min(1.0f, std::min(widthScale, heightScale));
    const ImVec2 imageSize(uvPickerSourceSize_.x * scale, uvPickerSourceSize_.y * scale);
    const ImVec2 imageMin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##decalUvPickerImage", imageSize, ImGuiButtonFlags_MouseButtonLeft);

    const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(imageMin, imageMax, IM_COL32(24, 24, 24, 255));
    drawList->AddImage(textureId, imageMin, imageMax);
    drawList->AddRect(imageMin, imageMax, IM_COL32(180, 180, 180, 255));

    const auto uvToScreen = [&](const ImVec2 uv) {
        return ImVec2(imageMin.x + uv.x * imageSize.x, imageMin.y + uv.y * imageSize.y);
    };
    const auto screenToUv = [&](const ImVec2 pos) {
        return ClampUvPoint(ImVec2(
            (pos.x - imageMin.x) / imageSize.x,
            (pos.y - imageMin.y) / imageSize.y));
    };

    const auto buildWindow = [&](const ImVec2 startUv, const ImVec2 endUv) {
        TerrainDecalUvWindow window = state.uvWindow;
        window.u1 = startUv.x;
        window.v1 = startUv.y;
        window.u2 = endUv.x;
        window.v2 = endUv.y;
        QuantizeUvWindowToPixels(window, uvPickerSourceSize_);
        return window;
    };

    if (ImGui::IsItemActivated()) {
        uvPickerDragging_ = true;
        uvPickerHadWindowBeforeDrag_ = state.hasUvWindow;
        uvPickerWindowBeforeDrag_ = state.uvWindow;
        uvPickerDragStartUv_ = screenToUv(ImGui::GetIO().MousePos);
    }

    const ImVec2 clampedMousePos(
        std::clamp(ImGui::GetIO().MousePos.x, imageMin.x, imageMax.x),
        std::clamp(ImGui::GetIO().MousePos.y, imageMin.y, imageMax.y));
    const ImVec2 dragStartScreen = uvToScreen(uvPickerDragStartUv_);
    const float dragDx = std::abs(clampedMousePos.x - dragStartScreen.x);
    const float dragDy = std::abs(clampedMousePos.y - dragStartScreen.y);
    const bool hasMeaningfulDrag = dragDx >= kUvPickerMinDragPixels || dragDy >= kUvPickerMinDragPixels;

    if (uvPickerDragging_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && hasMeaningfulDrag) {
        state.hasUvWindow = true;
        state.uvWindow = buildWindow(uvPickerDragStartUv_, screenToUv(clampedMousePos));
    }

    if (uvPickerDragging_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (!hasMeaningfulDrag) {
            state.hasUvWindow = uvPickerHadWindowBeforeDrag_;
            state.uvWindow = uvPickerWindowBeforeDrag_;
        }
        uvPickerDragging_ = false;
    }

    if (state.hasUvWindow) {
        QuantizeUvWindowToPixels(state.uvWindow, uvPickerSourceSize_);

        const ImVec2 selectionMin = uvToScreen(ImVec2(state.uvWindow.u1, state.uvWindow.v1));
        const ImVec2 selectionMax = uvToScreen(ImVec2(state.uvWindow.u2, state.uvWindow.v2));

        drawList->AddRectFilled(
            imageMin,
            ImVec2(imageMax.x, selectionMin.y),
            IM_COL32(0, 0, 0, 72));
        drawList->AddRectFilled(
            ImVec2(imageMin.x, selectionMax.y),
            imageMax,
            IM_COL32(0, 0, 0, 72));
        drawList->AddRectFilled(
            ImVec2(imageMin.x, selectionMin.y),
            ImVec2(selectionMin.x, selectionMax.y),
            IM_COL32(0, 0, 0, 72));
        drawList->AddRectFilled(
            ImVec2(selectionMax.x, selectionMin.y),
            ImVec2(imageMax.x, selectionMax.y),
            IM_COL32(0, 0, 0, 72));

        drawList->AddRect(selectionMin, selectionMax, IM_COL32(255, 196, 64, 255), 0.0f, 0, 2.0f);

        const float handleRadius = 3.0f;
        drawList->AddCircleFilled(selectionMin, handleRadius, IM_COL32(255, 196, 64, 255));
        drawList->AddCircleFilled(ImVec2(selectionMax.x, selectionMin.y), handleRadius, IM_COL32(255, 196, 64, 255));
        drawList->AddCircleFilled(selectionMax, handleRadius, IM_COL32(255, 196, 64, 255));
        drawList->AddCircleFilled(ImVec2(selectionMin.x, selectionMax.y), handleRadius, IM_COL32(255, 196, 64, 255));

        int selectedX = 0;
        int selectedY = 0;
        int selectedPixelWidth = 0;
        int selectedPixelHeight = 0;
        GetUvPixelRect(state.uvWindow,
                       uvPickerSourceSize_,
                       selectedX,
                       selectedY,
                       selectedPixelWidth,
                       selectedPixelHeight);
        ImGui::TextDisabled("Selection X:%d Y:%d W:%d H:%d px",
                            selectedX,
                            selectedY,
                            selectedPixelWidth,
                            selectedPixelHeight);

        const float previewMaxWidth = std::min(140.0f, std::max(1.0f, ImGui::GetContentRegionAvail().x));
        const float previewScale = std::min(
            1.0f,
            std::min(previewMaxWidth / std::max(1.0f, static_cast<float>(selectedPixelWidth)),
                     96.0f / std::max(1.0f, static_cast<float>(selectedPixelHeight))));
        const ImVec2 previewSize(
            std::max(1.0f, selectedPixelWidth * previewScale),
            std::max(1.0f, selectedPixelHeight * previewScale));
        ImGui::Image(
            textureId,
            previewSize,
            ImVec2(state.uvWindow.u1, state.uvWindow.v1),
            ImVec2(state.uvWindow.u2, state.uvWindow.v2));
    }
}

void DecalPanelTab::QueuePaintForDecal_(const uint32_t instanceId) {
    if (pendingPaint_.instanceId != instanceId) {
        ClearUvPickerTexture_();
    }

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

void DecalPanelTab::ClearUvPickerTexture_() {
    uvPickerTexture_.Release();
    uvPickerSourceSize_ = ImVec2(0.0f, 0.0f);
    uvPickerTextureInstanceId_ = 0;
    uvPickerDragging_ = false;
    uvPickerHadWindowBeforeDrag_ = false;
    uvPickerWindowBeforeDrag_ = {};
    uvPickerDragStartUv_ = ImVec2(0.0f, 0.0f);
}

bool DecalPanelTab::EnsureUvPickerTextureLoaded_() {
    if (pendingPaint_.instanceId == 0) {
        return false;
    }

    if (uvPickerTextureInstanceId_ == pendingPaint_.instanceId) {
        return uvPickerTexture_.GetID() != nullptr;
    }

    ClearUvPickerTexture_();
    if (!LoadDecalTexture_(pendingPaint_.instanceId, uvPickerTexture_, uvPickerSourceSize_)) {
        return false;
    }

    uvPickerTextureInstanceId_ = pendingPaint_.instanceId;
    return uvPickerTexture_.GetID() != nullptr;
}

bool DecalPanelTab::LoadDecalTexture_(const uint32_t instanceId, ImGuiTexture& outTexture, ImVec2& outSourceSize) const {
    outSourceSize = ImVec2(0.0f, 0.0f);

    if (!pRM_ || !imguiService_) {
        return false;
    }

    const cGZPersistResourceKey key = MakeDecalTextureKey(instanceId);

    // Find the DB segment containing this key and read raw bytes
    cRZAutoRefCount<cIGZPersistDBSegment> segment;
    if (!pRM_->FindDBSegment(key, segment.AsPPObj()) || !segment) {
        LOG_DEBUG("DecalPanelTab: no segment for decal 0x{:08X}", instanceId);
        return false;
    }

    const uint32_t size = segment->GetRecordSize(key);
    if (size == 0) {
        LOG_WARN("DecalPanelTab: zero-size record for decal 0x{:08X}", instanceId);
        return false;
    }

    std::vector<uint8_t> buf(size);
    uint32_t readSize = size;
    if (segment->ReadRecord(key, buf.data(), readSize) == 0) {
        LOG_WARN("DecalPanelTab: failed to read record for decal 0x{:08X}", instanceId);
        return false;
    }

    // Parse FSH record
    auto parseResult = FSH::Reader::Parse(std::span<const uint8_t>(buf.data(), readSize));
    if (!parseResult) {
        LOG_WARN("DecalPanelTab: FSH parse failed for decal 0x{:08X}", instanceId);
        return false;
    }

    const FSH::Record& record = *parseResult;
    if (record.entries.empty() || record.entries[0].bitmaps.empty()) {
        LOG_WARN("DecalPanelTab: FSH has no bitmaps for decal 0x{:08X}", instanceId);
        return false;
    }

    // Use the first (highest-resolution) bitmap from the first entry
    const FSH::Bitmap& bitmap = record.entries[0].bitmaps[0];

    std::vector<uint8_t> rgba;
    if (!FSH::Reader::ConvertToRGBA8(bitmap, rgba)) {
        LOG_WARN("DecalPanelTab: RGBA conversion failed for decal 0x{:08X}", instanceId);
        return false;
    }

    // ImGuiTexture::Create expects BGRA8 - swap R and B channels
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        std::swap(rgba[i], rgba[i + 2]);
    }

    if (!outTexture.Create(imguiService_, bitmap.width, bitmap.height, rgba.data())) {
        LOG_WARN("DecalPanelTab: texture upload failed for decal 0x{:08X}", instanceId);
        return false;
    }

    outSourceSize = ImVec2(static_cast<float>(bitmap.width), static_cast<float>(bitmap.height));
    return true;
}

ImGuiTexture DecalPanelTab::LoadDecalThumbnail_(const uint32_t instanceId) const {
    ImGuiTexture texture;
    ImVec2 sourceSize;
    LoadDecalTexture_(instanceId, texture, sourceSize);
    return texture;
}
