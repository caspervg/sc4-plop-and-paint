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
    constexpr float kAlpha = 1.6f; // times the effect's own 0.5

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
}

ModelPreview::View ModelPreview::CurrentView(cISC4View3DWin* view3D) {
    // ponytail: cSC4CameraControl has no SDK header; zoom and rotation are at 0x108 and 0x10C
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

void ModelPreview::Show(const std::vector<Item>& items, const View view) {
    const size_t count = std::min(items.size(), kMaxPreviews);
    if (!Available() || !GrowTo_(count)) {
        Hide();
        return;
    }

    for (size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        const PropModel* prop = i < count ? PropModel_(items[i].typeID) : nullptr;
        // Models made for one view: the one for the prop's rotation relative to the camera, like
        // cSC4ModelMakerUtility::GetModelInstanceID.
        const int32_t viewSlot = prop && prop->perView ? (items[i].rotation - view.rotation) & 3 : 0;
        cIS3DModelInstance* model = prop ? Instance_(prop->Key(std::clamp(view.zoom, 0, 4), viewSlot)) : nullptr;
        if (!model) {
            if (slot.started) {
                slot.effect->Stop(cISC4VisualEffect::tTransitionType::Hard);
                slot.started = false;
            }
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
        slot.effect->SetEffectTransform(MakeTransform(item.position, prop->perView ? view.rotation : item.rotation));
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
    }
}

bool ModelPreview::GrowTo_(const size_t count) {
    while (slots_.size() < count) {
        cISC4VisualEffect* effect = nullptr;
        if (!effectsManager_->CreateVisualEffect(kTemplateEffect, &effect) || !effect) {
            LOG_WARN("ModelPreview: the game has no '{}' effect, model previews are off", kTemplateEffect);
            templateMissing_ = true;
            return false;
        }

        effect->SetFloatParams(ParamType::Alpha, &kAlpha, 1);
        SetColor(effect, true);

        Slot slot;
        slot.effect = cRZAutoRefCount<cISC4VisualEffect>(effect); // takes over the created reference
        slots_.push_back(std::move(slot));
        LOG_DEBUG("ModelPreview: created preview effect {}", slots_.size());
    }
    return true;
}

const PropModel* ModelPreview::PropModel_(const uint32_t typeID) {
    auto it = props_.find(typeID);
    if (it == props_.end()) {
        it = props_.emplace(typeID, modelLookup_ ? modelLookup_(typeID) : std::nullopt).first;
        if (!it->second) {
            LOG_WARN("ModelPreview: no model for 0x{:08X}", typeID);
        }
    }
    return it->second ? &*it->second : nullptr;
}

cIS3DModelInstance* ModelPreview::Instance_(const cGZPersistResourceKey& key) {
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
    slots_.clear();
    props_.clear();
    instances_.clear();
    effectsManager_.Reset();
}
