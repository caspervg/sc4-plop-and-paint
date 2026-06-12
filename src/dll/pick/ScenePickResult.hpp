#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "cISC4Occupant.h"
#include "cRZAutoRefCount.h"
#include "cS3DVector3.h"

enum class ScenePickMode : uint8_t {
    Prop = 0,
    Flora = 1,
    Decal = 2,
    Lot = 3,
};

enum class PickedPropSource : uint8_t {
    City = 0,
    Lot = 1,
    Street = 2,
};

struct PickedProp {
    cRZAutoRefCount<cISC4Occupant> occupant{};
    PickedPropSource source{PickedPropSource::City};
    cS3DVector3 position{};
    uint32_t propType{0};
    int32_t orientation{0};
};

struct PickedFlora {
    cRZAutoRefCount<cISC4Occupant> occupant{};
    cS3DVector3 position{};
    uint32_t floraType{0};
    int32_t orientation{0};
};

enum class PickedDecalSource : uint8_t {
    Decal = 0,
    LotBaseTexture = 1,
    LotOverlayTexture = 2,
};

struct PickedDecal {
    // Texture instance ID, normalized to the zoom-4 FSH variant.
    uint32_t instanceId{0};
    // Raw TerrainDecalId value; 0 for lot textures.
    uint32_t decalId{0};
    PickedDecalSource source{PickedDecalSource::Decal};
    cS3DVector3 position{};
    // World-space XZ footprint of the picked lot texture (axis-aligned for
    // 90-degree lot rotations); only valid when hasWorldRect is set.
    bool hasWorldRect{false};
    float worldMinX{0.0f};
    float worldMinZ{0.0f};
    float worldMaxX{0.0f};
    float worldMaxZ{0.0f};
};

struct PickedLot {
    // Lot configuration (exemplar) instance ID, as used by TriggerLotPlop.
    uint32_t lotInstanceId{0};
    std::string name{};
    cS3DVector3 position{};
    // World-space XZ footprint of the lot for the overlay highlight.
    bool hasWorldRect{false};
    float worldMinX{0.0f};
    float worldMinZ{0.0f};
    float worldMaxX{0.0f};
    float worldMaxZ{0.0f};
};

using ScenePickResult = std::variant<PickedProp, PickedFlora, PickedDecal, PickedLot>;
