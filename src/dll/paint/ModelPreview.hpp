#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../props/PropModelKey.hpp"
#include "cIS3DModelInstance.h"
#include "cISC4EffectsManager.h"
#include "cISC4VisualEffect.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"

class cISC4City;
class cISC4View3DWin;

// Model previews drawn as visual effects, so nothing is added to the city. Each preview is a
// copy of a vanilla effect with a single model particle, with the model swapped for the item's.
// The particles are short-lived and emitted again at the current transform, so the previews
// follow every move.
class ModelPreview {
public:
    struct Item {
        uint32_t typeID = 0;
        cS3DVector3 position{};
        int32_t rotation = 0; // quarter turns
        bool valid = true;
    };

    // The camera's zoom (0-4) and rotation (0-3), which pick the model of non-3D props.
    struct View {
        int32_t zoom = 4;
        int32_t rotation = 0;
    };

    using ModelLookup = std::function<std::optional<PropModel>(uint32_t typeID)>;

    [[nodiscard]] static View CurrentView(cISC4View3DWin* view3D);

    ModelPreview() = default;
    ~ModelPreview();
    ModelPreview(const ModelPreview&) = delete;
    ModelPreview& operator=(const ModelPreview&) = delete;

    void SetCity(cISC4City* city);
    void SetModelLookup(ModelLookup lookup);

    // Shows one preview per item, at most kMaxPreviews, and stops the others.
    void Show(const std::vector<Item>& items, View view);
    void Hide();

    // False without a city or when the game has no template effect.
    [[nodiscard]] bool Available() const;

    // False when Show would draw nothing for this item.
    [[nodiscard]] bool HasModel(uint32_t typeID) const;

    // The game's lot direction arrow on the ground, turned like the item. Hide stops it too.
    void SetArrow(bool visible, const cS3DVector3& position, int32_t rotation);

private:
    struct Slot {
        cRZAutoRefCount<cISC4VisualEffect> effect;
        cIS3DModelInstance* model = nullptr;
        bool valid = true;
        bool started = false;
    };

    const PropModel* PropModel_(uint32_t typeID) const;
    cIS3DModelInstance* Instance_(const cGZPersistResourceKey& key) const;
    bool GrowTo_(size_t count);
    void Release_();

    cRZAutoRefCount<cISC4EffectsManager> effectsManager_;
    ModelLookup modelLookup_;
    std::vector<Slot> slots_;
    cRZAutoRefCount<cISC4VisualEffect> arrow_;
    bool arrowStarted_ = false;
    mutable std::unordered_map<uint32_t, std::optional<PropModel>> props_;
    mutable std::unordered_map<uint64_t, cRZAutoRefCount<cIS3DModelInstance>> instances_; // null: no model
    bool templateMissing_ = false;
};
