#include "RecentSwapPanel.hpp"

#include <algorithm>
#include <cstdio>

#include "../RecentPaintHistory.hpp"
#include "../SC4PlopAndPaintDirector.hpp"
#include "../common/Constants.hpp"
#include "../common/Utils.hpp"
#include "../flora/FloraRepository.hpp"
#include "../props/PropRepository.hpp"
#include "imgui.h"
#include "imgui_internal.h"

namespace {
    constexpr ImU32 kPropButton = IM_COL32(55, 65, 90, 180);
    constexpr ImU32 kPropHovered = IM_COL32(70, 82, 110, 200);
    constexpr ImU32 kPropActive = IM_COL32(45, 55, 80, 220);
    constexpr ImU32 kFloraButton = IM_COL32(50, 80, 55, 180);
    constexpr ImU32 kFloraHovered = IM_COL32(65, 100, 70, 200);
    constexpr ImU32 kFloraActive = IM_COL32(40, 70, 45, 220);
    constexpr ImU32 kCurrentPropButton = IM_COL32(70, 90, 130, 200);
    constexpr ImU32 kCurrentPropHovered = IM_COL32(85, 108, 155, 220);
    constexpr ImU32 kCurrentFloraButton = IM_COL32(65, 115, 75, 200);
    constexpr ImU32 kCurrentFloraHovered = IM_COL32(80, 135, 90, 220);
    constexpr ImU32 kPropIndicator = IM_COL32(100, 140, 220, 220);
    constexpr ImU32 kFloraIndicator = IM_COL32(100, 200, 120, 220);

    constexpr float kSpacing = 4.0f;
    constexpr float kEdgeMargin = 8.0f;
    constexpr size_t kMaxQuickSwapSlots = 8;

    const char* PaletteLabelForSource(const RecentPaintEntry& entry) {
        switch (entry.sourceKind) {
        case RecentPaintEntry::SourceKind::PropAutoFamily:
        case RecentPaintEntry::SourceKind::PropUserFamily:
            return "Family";
        case RecentPaintEntry::SourceKind::FloraFamily:
            return "Family";
        case RecentPaintEntry::SourceKind::FloraChain:
            return "Chain";
        case RecentPaintEntry::SourceKind::SingleProp:
        case RecentPaintEntry::SourceKind::SingleFlora:
        default:
            return nullptr;
        }
    }

    std::string ResolveEntryDisplayName(const RecentPaintEntry& entry,
                                        const PropRepository* const props,
                                        const FloraRepository* const flora) {
        if (!entry.name.empty()) {
            return entry.name;
        }

        if (entry.kind == RecentPaintEntry::Kind::Prop) {
            if (const Prop* prop = props ? props->FindPropByInstanceId(entry.typeId) : nullptr) {
                if (!prop->visibleName.empty()) {
                    return prop->visibleName;
                }
                if (!prop->exemplarName.empty()) {
                    return prop->exemplarName;
                }
            }

            char buffer[24];
            std::snprintf(buffer, sizeof(buffer), "Prop 0x%08X", entry.typeId);
            return buffer;
        }

        if (const Flora* floraEntry = flora ? flora->FindFloraByInstanceId(entry.typeId) : nullptr) {
            if (!floraEntry->visibleName.empty()) {
                return floraEntry->visibleName;
            }
            if (!floraEntry->exemplarName.empty()) {
                return floraEntry->exemplarName;
            }
        }

        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "Flora 0x%08X", entry.typeId);
        return buffer;
    }
}

void RecentSwapPanel::OnRender() {
    if (!visible_ || !director_) {
        return;
    }

    if (imguiService_) {
        const uint32_t currentGeneration = imguiService_->GetDeviceGeneration();
        if (currentGeneration != lastDeviceGeneration_) {
            thumbnailCache_.OnDeviceReset();
            lastDeviceGeneration_ = currentGeneration;
        }
    }

    if (!director_->GetActivePainterControl()) {
        return;
    }

    const auto& history = director_->GetRecentPaintHistory();
    if (history.Empty()) {
        return;
    }

    const auto& entries = history.Entries();
    const float thumbSize = UI::kIconSize;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float panelWidth = thumbSize + ImGui::GetStyle().WindowPadding.x * 2;
    const float entryHeight = thumbSize + kSpacing;
    const float panelHeight = static_cast<float>(entries.size()) * entryHeight
        - kSpacing
        + ImGui::GetStyle().WindowPadding.y * 2;

    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - panelWidth - kEdgeMargin,
               viewport->WorkPos.y + (viewport->WorkSize.y - panelHeight) * 0.5f),
        ImGuiCond_Always);

    ImGui::SetNextWindowBgAlpha(0.6f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar;

    if (!ImGui::Begin("##RecentSwapPanel", nullptr, kFlags)) {
        ImGui::End();
        return;
    }

    std::optional<size_t> pendingSwapIndex;

    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const bool isCurrent = i == 0;
        const bool isFlora = entry.kind == RecentPaintEntry::Kind::Flora;

        ImGui::PushID(static_cast<int>(i));

        const ImU32 buttonColor = isCurrent
            ? (isFlora ? kCurrentFloraButton : kCurrentPropButton)
            : (isFlora ? kFloraButton : kPropButton);
        const ImU32 hoveredColor = isCurrent
            ? (isFlora ? kCurrentFloraHovered : kCurrentPropHovered)
            : (isFlora ? kFloraHovered : kPropHovered);
        const ImU32 activeColor = isFlora ? kFloraActive : kPropActive;

        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoveredColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);

        if (entry.thumbnailKey != 0) {
            thumbnailCache_.Request(entry.thumbnailKey);
        }

        const ImVec2 buttonSize(thumbSize, thumbSize);
        bool clicked = false;
        auto thumbnailId = entry.thumbnailKey != 0 ? thumbnailCache_.Get(entry.thumbnailKey) : std::nullopt;
        const std::string displayName = ResolveEntryDisplayName(entry, props_, flora_);
        if (thumbnailId.has_value() && *thumbnailId != nullptr) {
            clicked = ImGui::ImageButton("##thumb", *thumbnailId, buttonSize);
        }
        else {
            const std::string shortName = displayName.length() > 3 ? displayName.substr(0, 3) : displayName;
            clicked = ImGui::Button(shortName.empty() ? "..." : shortName.c_str(), buttonSize);
        }

        ImGui::PopStyleColor(3);

        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (!isCurrent && i <= kMaxQuickSwapSlots) {
            char numLabel[4];
            std::snprintf(numLabel, sizeof(numLabel), "%zu", i);
            const ImVec2 textSize = ImGui::CalcTextSize(numLabel);
            constexpr float kPad = 2.0f;
            const ImVec2 textPos(rectMin.x + kPad, rectMax.y - textSize.y - kPad);
            drawList->AddRectFilled(
                ImVec2(textPos.x - 1.0f, textPos.y - 1.0f),
                ImVec2(textPos.x + textSize.x + 1.0f, textPos.y + textSize.y + 1.0f),
                IM_COL32(0, 0, 0, 160), 2.0f);
            drawList->AddText(textPos, IM_COL32(220, 220, 220, 230), numLabel);
        }

        if (isCurrent) {
            drawList->AddRectFilled(
                ImVec2(rectMin.x - 3.0f, rectMin.y + 2.0f),
                ImVec2(rectMin.x - 1.0f, rectMax.y - 2.0f),
                isFlora ? kFloraIndicator : kPropIndicator,
                1.0f);
        }

        if (ImGui::IsItemHovered()) {
            RenderTooltip_(i, isCurrent, isFlora);
        }

        if (clicked && !isCurrent) {
            pendingSwapIndex = i;
        }

        ImGui::PopID();
    }

    ImGui::End();

    if (pendingSwapIndex.has_value()) {
        ReleaseImGuiInputCapture_();
        director_->ActivateRecentPaint(*pendingSwapIndex);
    }

    thumbnailCache_.ProcessLoadQueue([this](const uint64_t key) {
        return LoadThumbnail_(key);
    });
}

void RecentSwapPanel::RenderTooltip_(const size_t index, const bool isCurrent, const bool isFlora) {
    const auto& entry = director_->GetRecentPaintHistory().Entries()[index];

    ImGui::BeginTooltip();
    const std::string displayName = ResolveEntryDisplayName(entry, props_, flora_);
    ImGui::TextUnformatted(displayName.c_str());

    if (!entry.palette.empty()) {
        if (const char* label = PaletteLabelForSource(entry)) {
            ImGui::TextDisabled("%s (%zu items)", label, entry.palette.size());
        }
        ImGui::Separator();

        const float kMemberThumbSize = std::max(24.0f, UI::kIconSize * 0.55f);
        constexpr size_t kMaxMembers = 8;
        constexpr size_t kMembersPerRow = 4;
        const size_t memberCount = std::min(entry.palette.size(), kMaxMembers);

        for (size_t memberIndex = 0; memberIndex < memberCount; ++memberIndex) {
            if (memberIndex > 0 && memberIndex % kMembersPerRow != 0) {
                ImGui::SameLine(0.0f, 2.0f);
            }

            const uint32_t memberId = entry.palette[memberIndex].propID.value();
            uint64_t memberKey = 0;
            if (!isFlora && props_) {
                if (const auto* prop = props_->FindPropByInstanceId(memberId)) {
                    memberKey = MakeGIKey(prop->groupId.value(), prop->instanceId.value());
                }
            }
            else if (isFlora && flora_) {
                if (const auto* flora = flora_->FindFloraByInstanceId(memberId)) {
                    memberKey = MakeGIKey(flora->groupId.value(), flora->instanceId.value());
                }
            }

            if (memberKey != 0) {
                thumbnailCache_.Request(memberKey);
                auto memberThumb = thumbnailCache_.Get(memberKey);
                if (memberThumb.has_value() && *memberThumb != nullptr) {
                    ImGui::Image(*memberThumb, ImVec2(kMemberThumbSize, kMemberThumbSize));
                    continue;
                }
            }

            ImGui::Dummy(ImVec2(kMemberThumbSize, kMemberThumbSize));
        }

        if (entry.palette.size() > kMaxMembers) {
            ImGui::TextDisabled("... and %zu more", entry.palette.size() - kMaxMembers);
        }
    }

    if (isCurrent) {
        ImGui::TextDisabled("(active)");
    }
    else if (index <= kMaxQuickSwapSlots) {
        ImGui::TextDisabled("Press %zu to swap", index);
    }
    else {
        ImGui::TextDisabled("Click to swap");
    }
    ImGui::EndTooltip();
}

ImGuiTexture RecentSwapPanel::LoadThumbnail_(const uint64_t key) {
    ImGuiTexture texture;
    if (!imguiService_) {
        return texture;
    }

    if (props_) {
        auto data = props_->GetPropThumbnailStore().LoadThumbnail(key);
        if (data.has_value()) {
            texture.Create(imguiService_, data->width, data->height, data->rgba.data());
            return texture;
        }
    }

    if (flora_) {
        auto data = flora_->GetFloraThumbnailStore().LoadThumbnail(key);
        if (data.has_value()) {
            texture.Create(imguiService_, data->width, data->height, data->rgba.data());
            return texture;
        }
    }

    return texture;
}

void RecentSwapPanel::ReleaseImGuiInputCapture_() {
    if (ImGui::GetCurrentContext()) {
        ImGui::ClearActiveID();
        ImGui::SetWindowFocus(nullptr);
    }
}
