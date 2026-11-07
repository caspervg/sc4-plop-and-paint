#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "cGZPersistResourceKey.h"
#include "cIS3DModelInstance.h"
#include "cISC4EffectsManager.h"
#include "cISC4VisualEffect.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"

class cISC4City;

// Model previews drawn as visual effects, so nothing is added to the city. Each preview is a
// copy of a vanilla effect with a single model particle, with the model swapped for the item's.
// The particles live for a few milliseconds and are emitted again at the current transform, so
// the previews follow every move.
class ModelPreview {
public:
    struct Item {
        uint32_t typeID = 0;
        cS3DVector3 position{};
        int32_t rotation = 0; // quarter turns
        bool valid = true;
    };

    using ModelKeyLookup = std::function<std::optional<cGZPersistResourceKey>(uint32_t typeID)>;

    ModelPreview() = default;
    ~ModelPreview();
    ModelPreview(const ModelPreview&) = delete;
    ModelPreview& operator=(const ModelPreview&) = delete;

    void SetCity(cISC4City* city);
    void SetModelKeyLookup(ModelKeyLookup lookup);

    // Shows one preview per item, at most kMaxPreviews, and stops the others.
    void Show(const std::vector<Item>& items);
    void Hide();

    // False without a city or when the game has no template effect.
    [[nodiscard]] bool Available() const;

private:
    struct Slot {
        cRZAutoRefCount<cISC4VisualEffect> effect;
        uint32_t typeID = 0;
        bool valid = true;
        bool started = false;
    };

    cIS3DModelInstance* Model_(uint32_t typeID);
    bool GrowTo_(size_t count);
    void Release_();

    cRZAutoRefCount<cISC4EffectsManager> effectsManager_;
    ModelKeyLookup modelKeyLookup_;
    std::vector<Slot> slots_;
    std::unordered_map<uint32_t, cRZAutoRefCount<cIS3DModelInstance>> models_; // null: no model
    bool templateMissing_ = false;
};
