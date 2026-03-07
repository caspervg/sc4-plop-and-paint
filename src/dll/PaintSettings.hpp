#pragma once
#include <cstdint>
#include <vector>

#include "../shared/entities.hpp"

enum class PropPaintMode {
    Direct = 0,
    Line = 1,
    Polygon = 2
};

enum class PropPreviewMode {
    Outline = 0,
    FullModel = 1,
    Combined = 2,
    Hidden = 3
};

struct PropPaintSettings {
    PropPaintMode mode = PropPaintMode::Direct;
    PropPreviewMode previewMode = PropPreviewMode::Outline;
    int32_t rotation = 0;
    float deltaYMeters = 0.0f;
    float spacingMeters = 5.0f;
    float densityPer100Sqm = 1.0f;
    float gridStepMeters = 16.0f;
    float randomOffset = 0.0f;
    bool alignToPath = false;
    bool randomRotation = false;
    bool showGrid = true;
    bool snapPointsToGrid = false;
    bool snapPlacementsToGrid = false;
    uint32_t randomSeed = 0;
    std::vector<FamilyEntry> activePalette{};
    float densityVariation = 0.0f;
};
