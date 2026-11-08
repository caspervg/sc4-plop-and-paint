#pragma once
#include <array>
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

    // The game's blue or red building footprint on the ground. rect is x1, z1, x2, z2 in meters.
    void SetFootprint(bool visible, const cS3DVector3& position, const std::array<float, 4>& rect, bool valid);

    // Draws models with pnp_preview_model from SC4PlopAndPaint.dat when the game has it. That
    // effect keeps the item's height. Set before the first Show.
    void UseUnpinnedModels() { unpinnedModels_ = true; }

    // Plays a short pop of the item's model. meters is the larger of the item's width and depth.
    void Pop(uint32_t typeID, const cS3DVector3& position, int32_t rotation, float meters, View view);

private:
    struct Slot {
        cRZAutoRefCount<cISC4VisualEffect> effect;
        cIS3DModelInstance* model = nullptr;
        bool valid = true;
        bool started = false;
    };

    const PropModel* PropModel_(uint32_t typeID) const;
    cIS3DModelInstance* Instance_(const cGZPersistResourceKey& key) const;
    cIS3DModelInstance* ModelFor_(const PropModel& prop, int32_t rotation, View view) const;
    bool GrowTo_(size_t count);
    void Release_();

    cRZAutoRefCount<cISC4EffectsManager> effectsManager_;
    ModelLookup modelLookup_;
    std::vector<Slot> slots_;
    cRZAutoRefCount<cISC4VisualEffect> arrow_;
    bool arrowStarted_ = false;
    struct Footprint {
        cRZAutoRefCount<cISC4VisualEffect> effect;
        bool started = false;
    };
    std::array<Footprint, 2> footprints_; // valid, invalid
    bool unpinnedModels_ = false;
    struct PopSlot {
        cRZAutoRefCount<cISC4VisualEffect> effect;
        const char* name = nullptr;
    };
    std::array<PopSlot, 8> pops_; // reused in turn, for fast clicks
    size_t nextPop_ = 0;
    mutable std::unordered_map<uint32_t, std::optional<PropModel>> props_;
    mutable std::unordered_map<uint64_t, cRZAutoRefCount<cIS3DModelInstance>> instances_; // null: no model
    bool templateMissing_ = false;
};
