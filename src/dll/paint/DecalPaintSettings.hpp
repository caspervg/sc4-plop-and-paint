#pragma once
#include "public/cIGZTerrainDecalService.h"

struct DecalPaintSettings {
    // Only Direct mode is used for decals.
    // decalInfo.center is filled in by PlaceAtWorld_(); all other fields
    // come from the user's settings modal.
    TerrainDecalState stateTemplate{};
};
