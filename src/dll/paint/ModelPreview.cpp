#include "ModelPreview.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "GZServPtrs.h"
#include "cIGZPersistResourceManager.h"
#include "cIS3DResModel.h"
#include "cISC43DRender.h"
#include "cISC4City.h"
#include "cISC4View3DWin.h"
#include "cS3DTransform.h"
#include "../utils/Logger.h"

namespace {
    // A puzzle piece preview: one model particle emitted every 0.1 s, drawn at size 1 and alpha 0.5,
    // pinned to the terrain.
    constexpr auto kTemplateEffect = "preview_el_straight";
    // The same without the terrain pin. From SC4PlopAndPaint.dat.
    constexpr auto kCursorModelEffect = "pnp_preview_model";

    constexpr auto kArrowEffect = "lot_direction_arrow";
    // The game's arrow decal is 10 m across. It shrinks with small items so that they stay in view,
    // and is never larger than the game draws it (cSC4DecalEffect::SetTransforms multiplies the
    // decal's size by the transform's scale).
    constexpr float kArrowFullMeters = 10.0f;
    constexpr float kArrowPerItemMeter = 1.5f;
    constexpr float kArrowMinScale = 0.2f;
    // pnp_footprint_notok is from SC4PlopAndPaint.dat, the vanilla red one is opaque with its own texture.
    constexpr const char* kFootprintEffects[2] = {"building_footprint_ok", "pnp_footprint_notok"};
    constexpr float kCellMeters = 16.0f; // DecalRect takes city cells: cSC4DecalEffect::SetFloatParams
    // ponytail: which way the arrow texture points at rotation 0 is not checked yet. Compare with
    // the outline's arrow in game and change this if they differ.
    constexpr int32_t kArrowQuarterTurns = 0;

    // ponytail: one effect per preview, each emitting 200 particles a second. Raise when that holds up.
    constexpr size_t kMaxPreviews = 512;

    constexpr float kValidColor[3] = {1.0f, 1.0f, 1.0f};
    constexpr float kInvalidColor[3] = {1.0f, 0.35f, 0.35f};

    constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0f;

    using ParamType = cISC4VisualEffect::tParamType;

    // Like cSC4ModelMaker::FindModelTransform: true 3D models turn with the prop, models made for
    // one view turn back against the camera.
    cS3DTransform MakeTransform(const cS3DVector3& position, const int32_t quarterTurns) {
        cS3DTransform transform(position);
        const float angle = -static_cast<float>(quarterTurns & 3) * kQuarterTurn;
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        transform.rotation[0][0] = c;
        transform.rotation[0][2] = s;
        transform.rotation[2][0] = -s;
        transform.rotation[2][2] = c;
        transform.flags |= cS3DTransform::kRotation;
        return transform;
    }

    void SetColor(cISC4VisualEffect* effect, const bool valid) {
        effect->SetFloatParams(ParamType::Color, valid ? kValidColor : kInvalidColor, 3);
    }

    int32_t ModelTurns(const PropModel& prop, const int32_t rotation, const ModelPreview::View view) {
        return prop.perView ? view.rotation : rotation;
    }

    // Small items grow more, so the pop stays visible. The effects are in SC4PlopAndPaint.dat.
    const char* PopEffect(const float meters) {
        return meters < 3.0f ? "pnp_pop_small" : meters < 8.0f ? "pnp_pop_medium" : "pnp_pop_large";
    }
}

ModelPreview::View ModelPreview::CurrentView(cISC4View3DWin* view3D) {
    // ponytail: cSC4CameraControl has no SDK header. Zoom and rotation are at 0x108 and 0x10C
    // (see sc4-render-services' camera-view-input sample).
    View view;
    cISC43DRender* renderer = view3D ? view3D->GetRenderer() : nullptr;
    const auto* camera = renderer ? reinterpret_cast<const uint8_t*>(renderer->GetCameraControl()) : nullptr;
    if (camera) {
        view.zoom = *reinterpret_cast<const int32_t*>(camera + 0x108);
        view.rotation = *reinterpret_cast<const int32_t*>(camera + 0x10C) & 3;
    }
    return view;
}

ModelPreview::~ModelPreview() {
    Release_();
}

void ModelPreview::SetCity(cISC4City* city) {
    Release_();
    templateMissing_ = false;
    if (city) {
        effectsManager_ = city->GetEffectsManager();
    }
}

void ModelPreview::SetModelLookup(ModelLookup lookup) {
    modelLookup_ = std::move(lookup);
    props_.clear();
}

bool ModelPreview::Available() const {
    return effectsManager_ && !templateMissing_;
}

bool ModelPreview::HasModel(const uint32_t typeID) const {
    const PropModel* prop = Available() ? PropModel_(typeID) : nullptr;
    return prop && Instance_(prop->Key(4, 0));
}

void ModelPreview::UpdateFootprint_(Slot& slot, const cS3DVector3& position, const std::array<float, 4>& rect,
                                    const bool valid) {
    const bool hasArea = rect[2] > rect[0] && rect[3] > rect[1];
    for (size_t i = 0; i < slot.footprints.size(); ++i) {
        Footprint& footprint = slot.footprints[i];
        if (!hasArea || !Available() || i != (valid ? 0u : 1u)) {
            if (footprint.started) {
                footprint.effect->Stop(cISC4VisualEffect::tTransitionType::Hard);
                footprint.started = false;
            }
            continue;
        }

        if (!footprint.effect) {
            cISC4VisualEffect* effect = nullptr;
            if (!effectsManager_->CreateVisualEffect(kFootprintEffects[i], &effect) || !effect) {
                continue;
            }
            footprint.effect = cRZAutoRefCount<cISC4VisualEffect>(effect); // takes over the created reference
        }

        const float cells[4] = {rect[0] / kCellMeters, rect[1] / kCellMeters, rect[2] / kCellMeters, rect[3] / kCellMeters};
        footprint.effect->SetFloatParams(ParamType::DecalRect, cells, 4);
        footprint.effect->SetEffectTransform(MakeTransform(position, 0));
        if (!footprint.started) {
            footprint.effect->Start(cISC4VisualEffect::tTransitionType::Soft);
            footprint.started = true;
        }
    }
}

void ModelPreview::SetArrow(const bool visible, const cS3DVector3& position, const int32_t rotation,
                            const float itemSizeMeters) {
    if (!visible || !Available()) {
        if (arrowStarted_) {
            arrow_->Stop(cISC4VisualEffect::tTransitionType::Hard);
            arrowStarted_ = false;
        }
        return;
    }

    if (!arrow_) {
        cISC4VisualEffect* effect = nullptr;
        if (!effectsManager_->CreateVisualEffect(kArrowEffect, &effect) || !effect) {
            return;
        }
        arrow_ = cRZAutoRefCount<cISC4VisualEffect>(effect); // takes over the created reference
    }

    cS3DTransform transform = MakeTransform(position, rotation + kArrowQuarterTurns);
    transform.scale = std::clamp(itemSizeMeters * kArrowPerItemMeter / kArrowFullMeters, kArrowMinScale, 1.0f);
    transform.flags |= cS3DTransform::kScale;
    arrow_->SetEffectTransform(transform);
    if (!arrowStarted_) {
        arrow_->Start(cISC4VisualEffect::tTransitionType::Soft);
        arrowStarted_ = true;
    }
}

void ModelPreview::Pop(const uint32_t typeID, const cS3DVector3& position, const int32_t rotation, const float meters,
                       const View view) {
    const PropModel* prop = Available() ? PropModel_(typeID) : nullptr;
    cIS3DModelInstance* model = prop ? ModelFor_(*prop, rotation, view) : nullptr;
    const char* name = PopEffect(meters);
    if (!model || !effectsManager_->HasVisualEffect(name)) {
        return;
    }

    auto pop = std::ranges::find_if(pops_, [name](PopSlot& slot) { return slot.name == name && !slot.effect->IsStarted(); });
    if (pop == pops_.end()) {
        cISC4VisualEffect* effect = nullptr;
        if (pops_.size() >= kMaxPreviews || !effectsManager_->CreateVisualEffect(name, &effect) || !effect) {
            return;
        }
        // takes over the created reference
        pops_.push_back({cRZAutoRefCount<cISC4VisualEffect>(effect), name});
        pop = std::prev(pops_.end());
    }

    void* instance = model;
    pop->effect->SetVoidParams(ParamType::Model, &instance, 1);
    pop->effect->SetEffectTransform(MakeTransform(position, ModelTurns(*prop, rotation, view)));
    pop->effect->Start(cISC4VisualEffect::tTransitionType::Soft);
}

void ModelPreview::Show(const std::vector<Item>& items, const View view) {
    const size_t count = std::min(items.size(), kMaxPreviews);
    if (!Available() || !GrowTo_(count)) {
        Hide();
        return;
    }

    for (size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        const PropModel* prop = i < count ? PropModel_(items[i].typeID) : nullptr;
        cIS3DModelInstance* model = prop ? ModelFor_(*prop, items[i].rotation, view) : nullptr;
        if (!model) {
            if (slot.started) {
                slot.effect->Stop(cISC4VisualEffect::tTransitionType::Hard);
                slot.started = false;
            }
            UpdateFootprint_(slot, {}, {}, true);
            continue;
        }

        const Item& item = items[i];
        if (slot.model != model) {
            void* instance = model;
            slot.effect->SetVoidParams(ParamType::Model, &instance, 1);
            slot.model = model;
        }
        if (slot.valid != item.valid) {
            SetColor(slot.effect, item.valid);
            slot.valid = item.valid;
        }
        slot.effect->SetEffectTransform(MakeTransform(item.position, ModelTurns(*prop, item.rotation, view)));
        UpdateFootprint_(slot, item.position, item.footprint, item.valid);
        if (!slot.started) {
            slot.effect->Start(cISC4VisualEffect::tTransitionType::Soft);
            slot.started = true;
            if (!slot.effect->IsStarted()) {
                LOG_WARN("ModelPreview: preview effect {} did not start", i + 1);
            }
        }
    }
}

void ModelPreview::Hide() {
    for (Slot& slot : slots_) {
        if (slot.started) {
            slot.effect->Stop(cISC4VisualEffect::tTransitionType::Hard);
            slot.started = false;
        }
        UpdateFootprint_(slot, {}, {}, true);
    }
    SetArrow(false, {}, 0, 0.0f);
}

bool ModelPreview::GrowTo_(const size_t count) {
    const char* name = unpinnedModels_ && effectsManager_->HasVisualEffect(kCursorModelEffect) ? kCursorModelEffect : kTemplateEffect;
    while (slots_.size() < count) {
        cISC4VisualEffect* effect = nullptr;
        if (!effectsManager_->CreateVisualEffect(name, &effect) || !effect) {
            LOG_WARN("ModelPreview: the game has no '{}' effect, model previews are off", name);
            templateMissing_ = true;
            return false;
        }

        SetColor(effect, true);

        Slot slot;
        slot.effect = cRZAutoRefCount<cISC4VisualEffect>(effect); // takes over the created reference
        slots_.push_back(std::move(slot));
        LOG_DEBUG("ModelPreview: created preview effect {}", slots_.size());
    }
    return true;
}

cIS3DModelInstance* ModelPreview::ModelFor_(const PropModel& prop, const int32_t rotation, const View view) const {
    // Models made for one view: the one for the prop's rotation relative to the camera, like
    // cSC4ModelMakerUtility::GetModelInstanceID.
    const int32_t viewSlot = prop.perView ? (rotation - view.rotation) & 3 : 0;
    return Instance_(prop.Key(std::clamp(view.zoom, 0, 4), viewSlot));
}

const PropModel* ModelPreview::PropModel_(const uint32_t typeID) const {
    auto it = props_.find(typeID);
    if (it == props_.end()) {
        it = props_.emplace(typeID, modelLookup_ ? modelLookup_(typeID) : std::nullopt).first;
        if (!it->second) {
            LOG_WARN("ModelPreview: no model for 0x{:08X}", typeID);
        }
    }
    return it->second ? &*it->second : nullptr;
}

cIS3DModelInstance* ModelPreview::Instance_(const cGZPersistResourceKey& key) const {
    const uint64_t id = static_cast<uint64_t>(key.group) << 32 | key.instance;
    if (const auto it = instances_.find(id); it != instances_.end()) {
        return it->second;
    }

    cRZAutoRefCount<cIS3DModelInstance>& cached = instances_[id];
    cIGZPersistResourceManagerPtr resourceManager;
    cRZAutoRefCount<cIS3DResModel> resModel;
    if (!resourceManager ||
        !resourceManager->GetResource(key, GZIID_cIS3DResModel, resModel.AsPPVoid(), 0, nullptr) || !resModel) {
        LOG_WARN("ModelPreview: no model 0x{:08X}-0x{:08X}-0x{:08X}", key.type, key.group, key.instance);
        return nullptr;
    }

    cIS3DModelInstance* instance = nullptr;
    if (!resModel->GetModelInstance(0, &instance) || !instance) {
        LOG_WARN("ModelPreview: model 0x{:08X}-0x{:08X} has no instance", key.group, key.instance);
        return nullptr;
    }
    cached = cRZAutoRefCount<cIS3DModelInstance>(instance);
    return cached;
}

void ModelPreview::Release_() {
    Hide();
    arrow_.Reset();
    for (PopSlot& pop : pops_) {
        if (pop.effect->IsStarted()) {
            pop.effect->Stop(cISC4VisualEffect::tTransitionType::Hard);
        }
    }
    pops_.clear();
    slots_.clear();
    props_.clear();
    instances_.clear();
    effectsManager_.Reset();
}
