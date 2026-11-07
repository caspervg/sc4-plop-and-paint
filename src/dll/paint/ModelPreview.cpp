#include "ModelPreview.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "GZServPtrs.h"
#include "cIGZPersistResourceManager.h"
#include "cIS3DResModel.h"
#include "cISC4City.h"
#include "cS3DTransform.h"
#include "../utils/Logger.h"

namespace {
    // The network tool's cursor: one model particle, not pinned to the terrain, emitted 200 times a
    // second with a life of 5 ms. It draws its model at size 4.
    constexpr auto kTemplateEffect = "road_tool_inactive";
    constexpr float kTemplateSize = 4.0f;

    // ponytail: one effect per preview, each emitting 200 particles a second. Raise when that holds up.
    constexpr size_t kMaxPreviews = 512;

    constexpr float kAlpha = 0.6f;
    constexpr float kValidColor[3] = {1.0f, 1.0f, 1.0f};
    constexpr float kInvalidColor[3] = {1.0f, 0.35f, 0.35f};

    // Same direction as the game's own lot placement footprint. Flip the sign if previews turn
    // the other way than placed props.
    constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0f;

    using ParamType = cISC4VisualEffect::tParamType;

    cS3DTransform MakeTransform(const cS3DVector3& position, const int32_t rotation) {
        cS3DTransform transform(position);
        const float angle = static_cast<float>(rotation & 3) * kQuarterTurn;
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

void ModelPreview::SetModelKeyLookup(ModelKeyLookup lookup) {
    modelKeyLookup_ = std::move(lookup);
    models_.clear();
}

bool ModelPreview::Available() const {
    return effectsManager_ && !templateMissing_;
}

void ModelPreview::Show(const std::vector<Item>& items) {
    const size_t count = std::min(items.size(), kMaxPreviews);
    if (!Available() || !GrowTo_(count)) {
        Hide();
        return;
    }

    for (size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        cIS3DModelInstance* model = i < count ? Model_(items[i].typeID) : nullptr;
        if (!model) {
            if (slot.started) {
                slot.effect->Stop(cISC4VisualEffect::tTransitionType::Hard);
                slot.started = false;
            }
            continue;
        }

        const Item& item = items[i];
        if (slot.typeID != item.typeID) {
            void* instance = model;
            slot.effect->SetVoidParams(ParamType::Model, &instance, 1);
            slot.typeID = item.typeID;
        }
        if (slot.valid != item.valid) {
            SetColor(slot.effect, item.valid);
            slot.valid = item.valid;
        }
        slot.effect->SetEffectTransform(MakeTransform(item.position, item.rotation));
        if (!slot.started) {
            slot.effect->Start(cISC4VisualEffect::tTransitionType::Hard);
            slot.started = true;
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

        const float size = 1.0f / kTemplateSize;
        effect->SetFloatParams(ParamType::Size, &size, 1);
        effect->SetFloatParams(ParamType::Alpha, &kAlpha, 1);
        SetColor(effect, true);

        Slot slot;
        slot.effect = cRZAutoRefCount<cISC4VisualEffect>(effect); // takes over the created reference
        slots_.push_back(std::move(slot));
    }
    return true;
}

cIS3DModelInstance* ModelPreview::Model_(const uint32_t typeID) {
    if (const auto it = models_.find(typeID); it != models_.end()) {
        return it->second;
    }

    cRZAutoRefCount<cIS3DModelInstance>& cached = models_[typeID];
    const auto key = modelKeyLookup_ ? modelKeyLookup_(typeID) : std::nullopt;
    cIGZPersistResourceManagerPtr resourceManager;
    if (!key || !resourceManager) {
        return nullptr;
    }

    cRZAutoRefCount<cIS3DResModel> resModel;
    if (!resourceManager->GetResource(*key, GZIID_cIS3DResModel, resModel.AsPPVoid(), 0, nullptr) || !resModel) {
        LOG_WARN("ModelPreview: no model 0x{:08X}-0x{:08X}-0x{:08X} for 0x{:08X}",
                 key->type, key->group, key->instance, typeID);
        return nullptr;
    }

    cIS3DModelInstance* instance = nullptr;
    if (!resModel->GetModelInstance(0, &instance) || !instance) {
        return nullptr;
    }
    cached = cRZAutoRefCount<cIS3DModelInstance>(instance);
    return cached;
}

void ModelPreview::Release_() {
    Hide();
    slots_.clear();
    models_.clear();
    effectsManager_.Reset();
}
