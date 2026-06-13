#pragma once
#include "PaintSettings.hpp"
#include "public/cIGZTerrainDecalService.h"

inline PropPaintSettings MakeDefaultDecalPlacementSettings() {
    PropPaintSettings settings{};
    settings.mode = PaintMode::Direct;
    settings.previewMode = PreviewMode::Outline;
    settings.gridStepMeters = 8.0f;
    settings.snapPointsToGrid = true;
    settings.snapPlacementsToGrid = false;
    return settings;
}

struct DecalPaintSettings {
    // Only Direct mode is used for decals.
    // decalInfo.center is filled in by PlaceAtWorld_(); all other fields
    // come from the user's settings modal.
    TerrainDecalState stateTemplate{};
    PropPaintSettings placementSettings{MakeDefaultDecalPlacementSettings()};
};
