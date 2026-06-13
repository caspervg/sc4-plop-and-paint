#include "ScenePickStatusPanel.hpp"

#include <algorithm>
#include <variant>

#include "../common/Utils.hpp"
#include "../decals/DecalTextureLoader.hpp"
#include "../flora/FloraRepository.hpp"
#include "../lots/LotRepository.hpp"
#include "../props/PropRepository.hpp"
#include "GZServPtrs.h"
#include "cIGZPersistResourceManager.h"
#include "imgui.h"
#include "public/cIGZImGuiService.h"

namespace {
    constexpr float kThumbnailMaxSize = 96.0f;

    const char* PickedDecalLabel(const PickedDecalSource source) {
        switch (source) {
        case PickedDecalSource::Decal:             return "Decal";
        case PickedDecalSource::LotBaseTexture:    return "Base texture";
        case PickedDecalSource::LotOverlayTexture: return "Overlay texture";
        }
        return "Texture";
    }
}

void ScenePickStatusPanel::OnRender() {
    if (!picker_ || !picker_->IsActive()) {
        return;
    }

    const std::optional<ScenePickResult>& hovered = picker_->GetHoveredResult();
    if (!hovered) {
        return;
    }

    ImVec2 pos = ImGui::GetMousePos();
    pos.x += 18.0f;
    pos.y += 18.0f;
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowBgAlpha(0.7f);
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs;

    if (!ImGui::Begin("##ScenePickStatus", nullptr, kFlags)) {
        ImGui::End();
        return;
    }

    std::visit([](const auto& picked) {
        using T = std::decay_t<decltype(picked)>;
        if constexpr (std::is_same_v<T, PickedProp>) {
            ImGui::Text("Prop 0x%08X", picked.propType);
        }
        else if constexpr (std::is_same_v<T, PickedFlora>) {
            ImGui::Text("Flora 0x%08X", picked.floraType);
        }
        else if constexpr (std::is_same_v<T, PickedDecal>) {
            ImGui::Text("%s 0x%08X", PickedDecalLabel(picked.source), picked.instanceId);
        }
        else if constexpr (std::is_same_v<T, PickedLot>) {
            if (!picked.name.empty()) {
                ImGui::Text("Lot 0x%08X  %s", picked.lotInstanceId, picked.name.c_str());
            }
            else {
                ImGui::Text("Lot 0x%08X", picked.lotInstanceId);
            }
        }
    }, *hovered);

    RenderHoveredThumbnail_(*hovered);

    const uint32_t count = picker_->GetCandidateCount();
    if (count > 1) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::Text("%u / %u  Alt+scroll to cycle", picker_->GetCandidateIndex() + 1, count);
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

void ScenePickStatusPanel::RenderHoveredThumbnail_(const ScenePickResult& hovered) {
    if (!imguiService_) {
        return;
    }

    // Textures don't survive a device reset; reload after one.
    const uint32_t deviceGeneration = imguiService_->GetDeviceGeneration();
    if (deviceGeneration != lastDeviceGeneration_) {
        ClearThumbnail_();
        lastDeviceGeneration_ = deviceGeneration;
    }

    ThumbnailKind kind = ThumbnailKind::None;
    uint64_t key = 0;
    if (const auto* decal = std::get_if<PickedDecal>(&hovered)) {
        kind = ThumbnailKind::DecalTexture;
        key = decal->instanceId;
    }
    else if (const auto* prop = std::get_if<PickedProp>(&hovered)) {
        if (props_ != nullptr) {
            if (const Prop* entry = props_->FindPropByInstanceId(prop->propType)) {
                kind = ThumbnailKind::Prop;
                key = MakeGIKey(entry->groupId.value(), entry->instanceId.value());
            }
        }
    }
    else if (const auto* flora = std::get_if<PickedFlora>(&hovered)) {
        if (flora_ != nullptr) {
            if (const Flora* entry = flora_->FindFloraByInstanceId(flora->floraType)) {
                kind = ThumbnailKind::Flora;
                key = MakeGIKey(entry->groupId.value(), entry->instanceId.value());
            }
        }
    }
    else if (const auto* lot = std::get_if<PickedLot>(&hovered)) {
        if (lots_ != nullptr) {
            if (const Building* building = lots_->FindBuildingByLotInstanceId(lot->lotInstanceId)) {
                kind = ThumbnailKind::Building;
                key = MakeGIKey(building->groupId.value(), building->instanceId.value());
            }
        }
    }

    if (kind == ThumbnailKind::None) {
        return;
    }

    if (kind != thumbnailKind_ || key != thumbnailKey_) {
        ClearThumbnail_();
        thumbnailKind_ = kind;
        thumbnailKey_ = key;

        if (kind == ThumbnailKind::DecalTexture) {
            cIGZPersistResourceManagerPtr pRM;
            thumbnailValid_ = decals::LoadDecalTexture(static_cast<cIGZPersistResourceManager*>(pRM),
                                                       imguiService_, static_cast<uint32_t>(key),
                                                       thumbnail_, thumbnailSourceSize_);
        }
        else {
            ThumbnailStore& store = kind == ThumbnailKind::Prop
                ? props_->GetPropThumbnailStore()
                : kind == ThumbnailKind::Flora
                    ? flora_->GetFloraThumbnailStore()
                    : lots_->GetBuildingThumbnailStore();
            if (const auto data = store.LoadThumbnail(key)) {
                thumbnailValid_ = thumbnail_.Create(imguiService_, data->width, data->height, data->rgba.data());
                thumbnailSourceSize_ = ImVec2(static_cast<float>(data->width), static_cast<float>(data->height));
            }
        }
    }

    if (!thumbnailValid_ || thumbnail_.GetID() == nullptr ||
        thumbnailSourceSize_.x <= 0.0f || thumbnailSourceSize_.y <= 0.0f) {
        return;
    }

    const float scale = kThumbnailMaxSize / std::max(thumbnailSourceSize_.x, thumbnailSourceSize_.y);
    const ImVec2 size(thumbnailSourceSize_.x * scale, thumbnailSourceSize_.y * scale);
    ImGui::Image(reinterpret_cast<ImTextureID>(thumbnail_.GetID()), size);
}

void ScenePickStatusPanel::ClearThumbnail_() {
    thumbnail_.Release();
    thumbnailSourceSize_ = ImVec2(0.0f, 0.0f);
    thumbnailKind_ = ThumbnailKind::None;
    thumbnailKey_ = 0;
    thumbnailValid_ = false;
}
