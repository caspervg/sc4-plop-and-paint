#include "DecalPanelTab.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
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
#include "DecalTextureLoader.hpp"
#include "FSHReader.h"
#include "FractionalAngles.hpp"

using PanelUi::ButtonWidth;
using PanelUi::CheckboxWidth;
using PanelUi::kGroupGap;
using PanelUi::NextInRow;
using PanelUi::Tooltip;

namespace {
    constexpr float kIconSize = 64.0f;
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

    char HexDigitUpper(const uint32_t value, const int nibbleIndexFromLeft) {
        const int shift = (7 - nibbleIndexFromLeft) * 4;
        const uint32_t nibble = (value >> shift) & 0xFu;
        return static_cast<char>(nibble < 10 ? ('0' + nibble) : ('A' + (nibble - 10)));
    }

    bool MatchesHexPrefix(const uint32_t value, const char* const prefix, const size_t prefixLen) {
        if (!prefix || prefixLen == 0 || prefixLen > 8) {
            return true;
        }

        for (size_t i = 0; i < prefixLen; ++i) {
            if (HexDigitUpper(value, static_cast<int>(i)) !=
                std::toupper(static_cast<unsigned char>(prefix[i]))) {
                return false;
            }
        }

        return true;
    }

    bool MatchesWealthFilter(const uint32_t value, const int wealthFilter) {
        if (wealthFilter == 0) {
            return true;
        }

        const uint32_t wealthNibble = (value >> 12) & 0xFu;
        switch (wealthFilter) {
        case 1: return wealthNibble == 0;
        case 2: return wealthNibble == 1;
        case 3: return wealthNibble == 2;
        case 4: return wealthNibble == 3;
        case 5: return wealthNibble >= 4;
        default: return true;
        }
    }

    bool MatchesDilapidationFilter(const uint32_t value, const int dilapidationFilter) {
        if (dilapidationFilter == 0) {
            return true;
        }

        const uint32_t dilapidationNibble = (value >> 8) & 0xFu;
        if (dilapidationFilter == 1) {
            return dilapidationNibble != 1;
        }
        if (dilapidationFilter == 2) {
            return dilapidationNibble == 1;
        }

        return true;
    }

    TerrainDecalState MakeDefaultDecalState() {
        TerrainDecalState state;
        state.decalInfo.baseSize = 16.0f;
        state.opacity = 1.0f;
        // With the ground paints, so they sort together and live shadows fall on both.
        state.overlayType = cISTETerrainView::tOverlayManagerType::StaticLand;
        state.uvWindow.mode = TerrainDecalUvMode::ClipSubrect;
        return state;
    }

    SavedDecalPreset BuildSavedPreset(const TerrainDecalState& state) {
        SavedDecalPreset preset;
        preset.overlayType = static_cast<uint32_t>(state.overlayType);
        preset.decalInfo.baseSize = state.decalInfo.baseSize;
        preset.decalInfo.rotationTurns = state.decalInfo.rotationTurns;
        preset.decalInfo.aspectMultiplier = state.decalInfo.aspectMultiplier;
        preset.decalInfo.uvScaleU = state.decalInfo.uvScaleU;
        preset.decalInfo.uvScaleV = state.decalInfo.uvScaleV;
        preset.decalInfo.uvOffset = state.decalInfo.uvOffset;
        preset.decalInfo.unknown8 = state.decalInfo.unknown8;
        preset.opacity = state.opacity;
        preset.enabled = state.enabled;
        preset.color.x = state.color.fX;
        preset.color.y = state.color.fY;
        preset.color.z = state.color.fZ;
        preset.drawMode = state.drawMode;
        preset.flags = state.flags;
        preset.hasUvWindow = state.hasUvWindow;
        preset.uvWindow.u1 = state.uvWindow.u1;
        preset.uvWindow.v1 = state.uvWindow.v1;
        preset.uvWindow.u2 = state.uvWindow.u2;
        preset.uvWindow.v2 = state.uvWindow.v2;
        preset.uvWindow.mode = static_cast<uint32_t>(state.uvWindow.mode);
        preset.depthOffset = state.depthOffset;
        return preset;
    }

    void ApplySavedPreset(const SavedDecalPreset& preset, TerrainDecalState& state) {
        state.overlayType = static_cast<cISTETerrainView::tOverlayManagerType>(preset.overlayType);
        state.decalInfo.baseSize = preset.decalInfo.baseSize;
        state.decalInfo.rotationTurns = preset.decalInfo.rotationTurns;
        state.decalInfo.aspectMultiplier = preset.decalInfo.aspectMultiplier;
        state.decalInfo.uvScaleU = preset.decalInfo.uvScaleU;
        state.decalInfo.uvScaleV = preset.decalInfo.uvScaleV;
        state.decalInfo.uvOffset = preset.decalInfo.uvOffset;
        state.decalInfo.unknown8 = preset.decalInfo.unknown8;
        state.opacity = preset.opacity;
        state.enabled = preset.enabled;
        state.color = cS3DVector3(preset.color.x, preset.color.y, preset.color.z);
        state.drawMode = preset.drawMode;
        state.flags = preset.flags;
        state.hasUvWindow = preset.hasUvWindow;
        state.uvWindow.u1 = preset.uvWindow.u1;
        state.uvWindow.v1 = preset.uvWindow.v1;
        state.uvWindow.u2 = preset.uvWindow.u2;
        state.uvWindow.v2 = preset.uvWindow.v2;
        state.uvWindow.mode = static_cast<TerrainDecalUvMode>(preset.uvWindow.mode);
        state.depthOffset = preset.depthOffset;
    }
}

DecalPanelTab::DecalPanelTab(SC4PlopAndPaintDirector* director,
                              DecalRepository* decals,
                              FavoritesRepository* favorites,
                              cIGZPersistResourceManager* pRM,
                              cIGZImGuiService* imguiService)
    : PanelTab(director, nullptr, nullptr, favorites, imguiService)
    , decals_(decals)
    , pRM_(pRM) {
    pendingPaint_.settings.stateTemplate = MakeDefaultDecalState();
}

void DecalPanelTab::OnRender() {
    if (!decals_) {
        ImGui::TextUnformatted("Decal repository not available.");
        return;
    }

    if (!director_->IsDecalServiceAvailable()) {
        ImGui::TextWrapped("The terrain decal service is not available. Make sure that you use SC4 1.1.641 "
                           "and set EnableTerrainDecalService=true in SC4PlopAndPaint.ini.");
        return;
    }

    if (decals_->Count() == 0) {
        ImGui::TextWrapped("No decal textures found. Restart the game. If this message stays, check the SC4PlopAndPaint log.");
        return;
    }

    RenderFilterBar_();
    ImGui::Separator();

    RefreshShown_();
    RenderResultLine_();
    RenderTools_();
    RenderSelectionLine_();

    if (selectedInstanceId_ != 0 &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
        QueuePaintForDecal_(selectedInstanceId_);
    }

    ImGui::Separator();
    if (shown_.empty()) {
        if (PanelUi::NoResults("No textures match these filters.")) {
            ClearFilters_();
        }
    }
    else {
        RenderDecalGrid_();
    }
    RenderSettingsModal_();
}

void DecalPanelTab::RenderResultLine_() {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Showing %zu of %zu textures", shown_.size(), decals_->Count());
    const int active = static_cast<int>(iidFilterBuf_[0] != '\0') + static_cast<int>(wealthFilter_ != 0) +
                       static_cast<int>(dilapidationFilter_ != 0) + static_cast<int>(favoritesOnly_) +
                       static_cast<int>(kindFilter_.Active());
    if (active > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d filter%s on", active, active == 1 ? "" : "s");
    }
    ImGui::SameLine(0.0f, kGroupGap);
    ImGui::BeginDisabled(active == 0);
    if (ImGui::SmallButton("Clear filters")) {
        ClearFilters_();
    }
    ImGui::EndDisabled();
    Tooltip(active == 0 ? "No filters are on." : "Show all textures again.");
}

void DecalPanelTab::ClearFilters_() {
    iidFilterBuf_[0] = '\0';
    wealthFilter_ = 0;
    dilapidationFilter_ = 0;
    favoritesOnly_ = false;
    kindFilter_ = {};
}

void DecalPanelTab::RenderTools_() {
    size_t index = 0;
    if (director_->IsDecalPainting()) {
        if (ImGui::SmallButton("Stop painting")) {
            director_->StopDecalPainting();
        }
        PanelUi::Tooltip(PanelUi::kStopPaintingHelp);
        ++index;
    }

    if (director_->IsDecalPicking()) {
        NextInRow(index++, ButtonWidth("Stop picking"));
        if (ImGui::SmallButton("Stop picking")) {
            director_->StopDecalPicking();
        }
    }
    else {
        NextInRow(index++, ButtonWidth("Pick decal"));
        if (ImGui::SmallButton("Pick decal")) {
            ReleaseImGuiInputCapture_();
            director_->StartDecalPicking([this](const uint32_t instanceId) {
                selectedInstanceId_ = instanceId;
                scrollToInstanceId_ = instanceId;
                // Filter to the texture family: the first four nibbles are the
                // base/overlay ID, the rest encode wealth/state/zoom variants.
                std::snprintf(iidFilterBuf_, sizeof(iidFilterBuf_), "%04X", instanceId >> 16);
                wealthFilter_ = 0;
                dilapidationFilter_ = 0;
                favoritesOnly_ = false;
                kindFilter_ = {};
            });
        }
        Tooltip("Click a decal, or the base or overlay texture of a lot,\nto select it in the list.\n"
                "If textures overlap, hold Alt and scroll to change between them.\nPress Esc to cancel.");
    }

    if (director_->IsDecalStripping()) {
        NextInRow(index, ButtonWidth("Stop stripping"), kGroupGap);
        if (ImGui::SmallButton("Stop stripping")) {
            director_->StopDecalStripping();
        }
    }
    else {
        NextInRow(index, ButtonWidth("Strip decals"), kGroupGap);
        if (ImGui::SmallButton("Strip decals")) {
            ReleaseImGuiInputCapture_();
            director_->StartDecalStripping();
        }
        Tooltip("Click a decal to remove it.\nPress B for brush mode.\nPress Ctrl+Z to undo.\nPress Esc to stop.");
    }
}

void DecalPanelTab::RenderSelectionLine_() {
    const bool hasSelection = selectedInstanceId_ != 0;
    constexpr const char* kSelectFirst = "Click a texture first.";

    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Paint...")) {
        QueuePaintForDecal_(selectedInstanceId_);
    }
    ImGui::EndDisabled();
    Tooltip(hasSelection ? "Choose the settings, then paint. You can also press Enter." : kSelectFirst);

    NextInRow(1, ButtonWidth("Paint now"));
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("Paint now")) {
        StartPaintingDecal_(selectedInstanceId_);
    }
    ImGui::EndDisabled();
    Tooltip(hasSelection ? "Paint now with the settings you used last." : kSelectFirst);

    if (!hasSelection) {
        ImGui::SameLine(0.0f, kGroupGap);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Click a texture to select it.");
        return;
    }

    const bool isFavorite = favorites_ && favorites_->IsDecalFavorite(selectedInstanceId_);
    if (favorites_) {
        NextInRow(2, ButtonWidth("Unstar"), kGroupGap);
        if (ImGui::Button(isFavorite ? "Unstar" : "Star")) {
            favorites_->ToggleDecalFavorite(selectedInstanceId_);
        }
        Tooltip(isFavorite ? "Remove from favorites" : "Add to favorites");
    }

    const auto* presets = favorites_ ? favorites_->GetDecalFavoritePresets(selectedInstanceId_) : nullptr;
    char text[48];
    std::snprintf(text, sizeof(text), "0x%08X%s", selectedInstanceId_,
                  presets && !presets->empty() ? ", has saved presets" : "");
    NextInRow(3, ImGui::CalcTextSize(text).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", text);
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
    ImGui::SetNextItemWidth(UI::iidFilterWidth());
    ImGui::InputTextWithHint("##DecalIID", "IID prefix...", iidFilterBuf_, sizeof(iidFilterBuf_));
    Tooltip("Type the start of a hex instance ID, for example 25A7.");

    NextInRow(1, UI::dropdownWidth());
    static constexpr const char* kWealthFilterLabels[] = {"Any wealth", "None", "$", "$$", "$$$", "Other"};
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    ImGui::Combo("##DecalWealthFilter", &wealthFilter_, kWealthFilterLabels, IM_ARRAYSIZE(kWealthFilterLabels));
    Tooltip("Show textures with this wealth in their instance ID.");

    NextInRow(2, UI::dropdownWidth());
    static constexpr const char* kDilapidationFilterLabels[] = {"Any state", "Normal", "Dilapidated"};
    ImGui::SetNextItemWidth(UI::dropdownWidth());
    ImGui::Combo("##DecalDilapidationFilter",
                 &dilapidationFilter_,
                 kDilapidationFilterLabels,
                 IM_ARRAYSIZE(kDilapidationFilterLabels));
    Tooltip("Show textures with this state in their instance ID.");

    NextInRow(3, CheckboxWidth("Favorites only"));
    ImGui::Checkbox("Favorites only##decalFavoritesOnly", &favoritesOnly_);
    Tooltip("Show only the textures that you starred.");

    NextInRow(4, CheckboxWidth("Base") + ImGui::GetStyle().ItemSpacing.x + CheckboxWidth("Overlay"));
    if (kindFilter_.Render(*decals_)) {
        shownDirty_ = true;
    }
}

void DecalPanelTab::RefreshShown_() {
    const size_t favoriteCount = favoritesOnly_ && favorites_ ? favorites_->GetFavoriteDecalIds().size() : 0;
    if (!shownDirty_ && appliedIid_ == iidFilterBuf_ && appliedFavoritesOnly_ == favoritesOnly_ &&
        appliedFavoriteCount_ == favoriteCount && appliedWealth_ == wealthFilter_ &&
        appliedDilapidation_ == dilapidationFilter_ && appliedBase_ == kindFilter_.base &&
        appliedOverlay_ == kindFilter_.overlay && appliedTotal_ == decals_->Count()) {
        return;
    }
    shownDirty_ = false;
    appliedIid_ = iidFilterBuf_;
    appliedFavoritesOnly_ = favoritesOnly_;
    appliedFavoriteCount_ = favoriteCount;
    appliedWealth_ = wealthFilter_;
    appliedDilapidation_ = dilapidationFilter_;
    appliedBase_ = kindFilter_.base;
    appliedOverlay_ = kindFilter_.overlay;
    appliedTotal_ = decals_->Count();
    BuildFilteredIndices_(shown_);
}

void DecalPanelTab::BuildFilteredIndices_(std::vector<size_t>& out) const {
    const auto& ids = decals_->GetInstanceIds();
    out.clear();
    out.reserve(ids.size());

    const size_t iidFilterLen = std::strlen(iidFilterBuf_);
    const auto* favoriteDecalIds = favorites_ ? &favorites_->GetFavoriteDecalIds() : nullptr;

    for (size_t i = 0; i < ids.size(); ++i) {
        const uint32_t instanceId = ids[i];
        if (favoritesOnly_ && (!favoriteDecalIds || !favoriteDecalIds->contains(ids[i]))) {
            continue;
        }
        if (!MatchesHexPrefix(instanceId, iidFilterBuf_, iidFilterLen)) {
            continue;
        }
        if (!MatchesWealthFilter(instanceId, wealthFilter_)) {
            continue;
        }
        if (!MatchesDilapidationFilter(instanceId, dilapidationFilter_)) {
            continue;
        }
        if (!kindFilter_.Allows(*decals_, instanceId)) {
            continue;
        }
        out.push_back(i);
    }
}

void DecalPanelTab::RenderDecalGrid_() {
    const auto& ids = decals_->GetInstanceIds();
    const std::vector<size_t>& indices = shown_;

    if (!ImGui::BeginChild("DecalGrid", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        ImGui::EndChild();
        return;
    }

    if (scrollToInstanceId_ != 0) {
        for (size_t i = 0; i < indices.size(); ++i) {
            if (ids[indices[i]] == scrollToInstanceId_) {
                ImGui::SetScrollY(ThumbnailGrid::ScrollYFor(i, kIconSize));
                break;
            }
        }
        scrollToInstanceId_ = 0;
    }

    const size_t visible = ThumbnailGrid::Render(indices.size(), kIconSize,
        [&](const size_t index) { return ids[indices[index]]; },
        [&](const size_t index) {
        const uint32_t instanceId = ids[indices[index]];
        const bool isFavorite = favorites_ && favorites_->IsDecalFavorite(instanceId);
        const bool hasSavedPresets =
            favorites_ && favorites_->GetDecalFavoritePresets(instanceId) &&
            !favorites_->GetDecalFavoritePresets(instanceId)->empty();

        const auto texture = thumbnailCache_.Get(instanceId);
        if (!texture) {
            thumbnailCache_.Request(instanceId);
        }
        RenderGridTile_(texture);
        ThumbnailGrid::DecorateLastTile(instanceId == selectedInstanceId_, isFavorite, hasSavedPresets);

        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool dblClicked = clicked && ImGui::GetMouseClickedCount(ImGuiMouseButton_Left) >= 2;

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0x%08X%s\n%s\nDouble-click for the paint settings.\nRight-click for more.",
                              instanceId,
                              !isFavorite ? "" : hasSavedPresets ? ", favorite with saved presets" : ", favorite",
                              director_->IsDecalPainting() ? "Click to switch to this decal." : "Click to select.");
        }

        if (dblClicked) {
            selectedInstanceId_ = instanceId;
            QueuePaintForDecal_(instanceId);
        }
        else if (clicked) {
            selectedInstanceId_ = instanceId;
            if (director_->IsDecalPainting()) {
                StartPaintingDecal_(instanceId);
            }
        }

        if (ImGui::BeginPopupContextItem("##DecalTileMenu")) {
            selectedInstanceId_ = instanceId;
            if (ImGui::Selectable("Paint...")) {
                QueuePaintForDecal_(instanceId);
            }
            if (ImGui::Selectable("Paint now")) {
                StartPaintingDecal_(instanceId);
            }
            if (favorites_ && ImGui::Selectable(isFavorite ? "Remove from favorites" : "Add to favorites")) {
                favorites_->ToggleDecalFavorite(instanceId);
            }
            ImGui::EndPopup();
        }
    });
    thumbnailCache_.FitVisible(visible + Cache::kMaxLoadPerFrame);

    thumbnailCache_.ProcessLoadQueue([this](const uint32_t instanceId) {
        return LoadDecalThumbnail_(instanceId);
    });

    ImGui::EndChild();
}

std::string DecalPanelTab::CurrentFaAngleLabel_(const float rotationDegrees) const {
    return fa_angles::Label(rotationDegrees);
}

void DecalPanelTab::RenderFaAngleCombo_(float& rotationDegrees, TerrainDecalState& state) {
    const float quadrantBase = std::floor(rotationDegrees / 90.0f) * 90.0f;
    const std::string previewLabel = CurrentFaAngleLabel_(rotationDegrees);
    const float quadrantOffset = rotationDegrees - quadrantBase;

    if (!ImGui::BeginCombo("FA angle##decalFaAngle", previewLabel.c_str())) {
        return;
    }

    for (const fa_angles::Preset& preset : fa_angles::kQuadrantPresets) {
        const bool isSelected = std::fabs(preset.degrees - quadrantOffset) < 0.05f;
        if (ImGui::Selectable(preset.label.data(), isSelected)) {
            rotationDegrees = quadrantBase + preset.degrees;
            state.decalInfo.rotationTurns = DegreesToTurns(rotationDegrees);
        }
        if (isSelected) {
            ImGui::SetItemDefaultFocus();
        }
    }
    ImGui::EndCombo();
}

void DecalPanelTab::RenderSettingsModal_() {
    if (pendingPaint_.open) {
        dialogState_ = pendingPaint_.settings.stateTemplate;
        ImGui::OpenPopup("Decal Paint Settings");
        pendingPaint_.open = false;
    }

    // With its sections open it scrolls instead of leaving the screen.
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.9f));
    if (!ImGui::BeginPopupModal("Decal Paint Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    TerrainDecalState& state = dialogState_;
    float rotationDegrees = TurnsToDegrees(state.decalInfo.rotationTurns);

    if (const auto texture = thumbnailCache_.Get(pendingPaint_.instanceId)) {
        ImGui::Image(*texture, ImVec2(kIconSize, kIconSize));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::Text("Texture: 0x%08X", pendingPaint_.instanceId);
    if (ImGui::SmallButton("Reset settings")) {
        state = MakeDefaultDecalState();
        state.textureKey = MakeDecalTextureKey(pendingPaint_.instanceId);
        ResetPresetNameInput_();
    }
    Tooltip("Set all the settings back to their defaults.");
    ImGui::EndGroup();
    ImGui::Separator();

    ImGui::PushItemWidth(ImGui::GetFontSize() * 16.0f);
    ImGui::DragFloat("Base size (m)##decalSize", &state.decalInfo.baseSize, 1.0f, 0.5f, 512.0f, "%.1f",
                     ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::DragFloat("Rotation (deg)##decalRot", &rotationDegrees, 1.0f, -3600.0f, 3600.0f, "%.1f")) {
        state.decalInfo.rotationTurns = DegreesToTurns(rotationDegrees);
    }
    RenderFaAngleCombo_(rotationDegrees, state);
    Tooltip("Angles that line up with fractional-angle roads.\nThe choice stays in the current quarter of the circle.");
    bool mirrored = (state.flags & kTerrainDecalFlagMirror) != 0;
    if (ImGui::Checkbox("Mirror##decalMirror", &mirrored)) {
        state.flags ^= kTerrainDecalFlagMirror;
    }
    ImGui::SliderFloat("Opacity##decalOpacity", &state.opacity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::ColorEdit3("Color tint##decalColor", &state.color.fX);

    // Rare settings, closed by default. Their state shows on the closed header.
    char header[48];
    std::snprintf(header, sizeof(header), "Depth offset: %s###decalDepth", state.depthOffset < 0 ? "default" : "custom");
    if (ImGui::CollapsingHeader(header)) {
        bool useDefaultDepthOffset = state.depthOffset < 0;
        if (ImGui::Checkbox("Use default depth offset##decalDefaultDepthOffset", &useDefaultDepthOffset)) {
            state.depthOffset = useDefaultDepthOffset ? -1 : 2;
        }
        ImGui::BeginDisabled(useDefaultDepthOffset);
        int depthOffset = std::max(0, state.depthOffset);
        if (ImGui::InputInt("Depth offset##decalDepthOffset", &depthOffset)) {
            state.depthOffset = std::clamp(depthOffset, 0, 64);
        }
        ImGui::EndDisabled();
        Tooltip("Raises or lowers the decal surface relative to the terrain.");
    }

    std::snprintf(header, sizeof(header), "Part of the texture: %s###decalUv", state.hasUvWindow ? "on" : "off");
    if (ImGui::CollapsingHeader(header)) {
        RenderUvSection_(state);
    }

    ImGui::Separator();
    RenderPresets_(state);
    ImGui::PopItemWidth();

    ImGui::Separator();
    if (RenderStartCancel_()) {
        pendingPaint_.settings.stateTemplate = dialogState_;
        char name[20];
        std::snprintf(name, sizeof(name), "0x%08X", pendingPaint_.instanceId);
        ReleaseImGuiInputCapture_();
        director_->StartDecalPainting(pendingPaint_.instanceId, pendingPaint_.settings, name);
    }

    ImGui::EndPopup();
}

void DecalPanelTab::RenderUvSection_(TerrainDecalState& state) {
    if (ImGui::Checkbox("Use only part of the texture##decalHasUvWindow", &state.hasUvWindow) && !state.hasUvWindow) {
        state.uvWindow = {};
        state.uvWindow.mode = TerrainDecalUvMode::ClipSubrect;
    }
    if (uvPickerSourceSize_.x > 0.0f && uvPickerSourceSize_.y > 0.0f) {
        ImGui::TextDisabled("Texture %.0f x %.0f px. Drag to select an area. The edges snap to the pixels.",
                            uvPickerSourceSize_.x,
                            uvPickerSourceSize_.y);
    }
    else {
        ImGui::TextDisabled("Drag on the texture to select an area.");
    }
    RenderUvPicker_(state);

    ImGui::BeginDisabled(!state.hasUvWindow);
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
    else if (ImGui::DragFloat4("UV bounds##decalUvWindow", &state.uvWindow.u1, 0.01f, 0.0f, 1.0f, "%.2f")) {
        // Without a texture size, these are the only fields there can be.
        NormalizeUvWindow(state.uvWindow);
    }
    const char* uvModes[] = {"Stretch to fit", "Clip"};
    int uvMode = static_cast<int>(state.uvWindow.mode);
    if (ImGui::Combo("Fit##decalUvMode", &uvMode, uvModes, IM_ARRAYSIZE(uvModes))) {
        state.uvWindow.mode = static_cast<TerrainDecalUvMode>(uvMode);
    }
    ImGui::EndDisabled();
}

void DecalPanelTab::RenderPresets_(const TerrainDecalState& state) {
    if (!favorites_) {
        return;
    }

    const auto* presets = favorites_->GetDecalFavoritePresets(pendingPaint_.instanceId);
    const int presetCount = presets ? static_cast<int>(presets->size()) : 0;
    if (selectedFavoritePresetIndex_ >= presetCount) {
        selectedFavoritePresetIndex_ = -1;
    }
    const bool hasChoice = selectedFavoritePresetIndex_ >= 0;

    const char* preview = presetCount == 0 ? "No saved presets"
                        : hasChoice ? (*presets)[selectedFavoritePresetIndex_].name.c_str()
                                    : "Choose a preset";
    ImGui::BeginDisabled(presetCount == 0);
    if (ImGui::BeginCombo("Preset##decalFavoritePresetList", preview)) {
        for (int i = 0; i < presetCount; ++i) {
            const bool selected = i == selectedFavoritePresetIndex_;
            std::string label = (*presets)[i].name;
            if ((*presets)[i].isDefault) {
                label += " [default]";
            }
            if (ImGui::Selectable(label.c_str(), selected)) {
                LoadFavoritePresetByName_(pendingPaint_.instanceId, (*presets)[i].name.c_str());
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    Tooltip(presetCount == 0 ? "Type a name below to save these settings as a preset."
                             : "Choose a preset to use its settings.");

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasChoice);
    if (ImGui::Button("Delete##decalPreset")) {
        ImGui::OpenPopup("##deleteDecalPreset");
    }
    ImGui::EndDisabled();
    Tooltip(hasChoice ? "Delete this preset." : "Choose a preset first.");

    // A copy: the repository erases the string that it would compare with.
    const std::string choice = hasChoice ? (*presets)[selectedFavoritePresetIndex_].name : std::string{};
    if (PanelUi::ConfirmDeletePopup("##deleteDecalPreset", choice.c_str()) &&
        favorites_->DeleteDecalFavoritePreset(pendingPaint_.instanceId, choice)) {
        ResetPresetNameInput_();
    }

    ImGui::InputTextWithHint("Preset name##decalFavoritePresetName",
                             "Name for these settings",
                             presetNameBuf_,
                             sizeof(presetNameBuf_));

    const auto save = [&](const bool makeDefault) {
        favorites_->UpsertDecalFavoritePreset(pendingPaint_.instanceId, presetNameBuf_, BuildSavedPreset(state), makeDefault);
        const auto* updated = favorites_->GetDecalFavoritePresets(pendingPaint_.instanceId);
        for (size_t i = 0; updated && i < updated->size(); ++i) {
            if ((*updated)[i].name == presetNameBuf_) {
                selectedFavoritePresetIndex_ = static_cast<int>(i);
                break;
            }
        }
    };

    const bool hasTypedName = presetNameBuf_[0] != '\0';
    ImGui::BeginDisabled(!hasTypedName);
    if (ImGui::Button("Save preset")) {
        save(false);
    }
    Tooltip(hasTypedName ? "Saves these settings and stars the texture.\nA preset with the same name is replaced."
                         : "Type a name first.");
    ImGui::EndDisabled();
    ImGui::SameLine();
    // Off for now: nothing applies a default preset since the call to ApplyFavoritePresetIfAvailable_ was removed.
    ImGui::BeginDisabled();
    if (ImGui::Button("Save Default")) {
        save(true);
    }
    ImGui::EndDisabled();
    Tooltip("Not available yet. Default presets are not used at the moment.");
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
        ResetPresetNameInput_();
    }

    pendingPaint_.instanceId = instanceId;
    UseLastPaintState_(instanceId);
    pendingPaint_.open = true;
}

void DecalPanelTab::UseLastPaintState_(const uint32_t instanceId) {
    if (const TerrainDecalState* last = director_->GetLastDecalPaintState()) {
        pendingPaint_.settings.stateTemplate = *last;
    }
    pendingPaint_.settings.stateTemplate.textureKey = MakeDecalTextureKey(instanceId);
}

void DecalPanelTab::StartPaintingDecal_(const uint32_t instanceId) {
    if (instanceId == 0) {
        return;
    }

    if (pendingPaint_.instanceId != instanceId) {
        ClearUvPickerTexture_();
        ResetPresetNameInput_();
    }

    pendingPaint_.instanceId = instanceId;
    UseLastPaintState_(instanceId);

    char name[20];
    std::snprintf(name, sizeof(name), "0x%08X", instanceId);

    ReleaseImGuiInputCapture_();
    director_->StartDecalPainting(instanceId, pendingPaint_.settings, name);
}

void DecalPanelTab::ApplyFavoritePresetIfAvailable_(const uint32_t instanceId) {
    if (!favorites_) {
        return;
    }

    if (const SavedDecalPreset* preset = favorites_->GetDefaultDecalFavoritePreset(instanceId)) {
        ApplySavedPreset(*preset, pendingPaint_.settings.stateTemplate);
        pendingPaint_.settings.stateTemplate.textureKey = MakeDecalTextureKey(instanceId);
    }
}

void DecalPanelTab::LoadFavoritePresetByName_(const uint32_t instanceId, const char* const presetName) {
    if (!favorites_ || !presetName || presetName[0] == '\0') {
        return;
    }

    const auto* presets = favorites_->GetDecalFavoritePresets(instanceId);
    if (!presets) {
        return;
    }

    for (size_t i = 0; i < presets->size(); ++i) {
        if ((*presets)[i].name == presetName) {
            ApplySavedPreset((*presets)[i].preset, dialogState_);
            dialogState_.textureKey = MakeDecalTextureKey(instanceId);
            selectedFavoritePresetIndex_ = static_cast<int>(i);
            std::snprintf(presetNameBuf_, sizeof(presetNameBuf_), "%s", presetName);
            return;
        }
    }
}

void DecalPanelTab::ResetPresetNameInput_() {
    presetNameBuf_[0] = '\0';
    selectedFavoritePresetIndex_ = -1;
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
    uvPickerTextureInstanceId_ = pendingPaint_.instanceId;  // also on failure: no new attempt each frame
    return LoadDecalTexture_(pendingPaint_.instanceId, uvPickerTexture_, uvPickerSourceSize_) &&
           uvPickerTexture_.GetID() != nullptr;
}

bool DecalPanelTab::LoadDecalTexture_(const uint32_t instanceId, ImGuiTexture& outTexture, ImVec2& outSourceSize) const {
    outSourceSize = ImVec2(0.0f, 0.0f);
    return decals::LoadDecalTexture(pRM_, imguiService_, instanceId, outTexture, outSourceSize);
}

ImGuiTexture DecalPanelTab::LoadDecalThumbnail_(const uint32_t instanceId) const {
    ImGuiTexture texture;
    ImVec2 sourceSize;
    LoadDecalTexture_(instanceId, texture, sourceSize);
    return texture;
}
