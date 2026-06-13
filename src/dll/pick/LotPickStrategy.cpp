#include "LotPickStrategy.hpp"

#include "SC4Rect.h"
#include "cISC4Lot.h"
#include "cISC4LotConfiguration.h"
#include "cISC4LotManager.h"
#include "cRZBaseString.h"

namespace {
    constexpr float kPickRadiusMeters = 2.0f;
    constexpr float kCellSizeMeters = 16.0f;
}

ScenePickMode LotPickStrategy::Mode() const {
    return ScenePickMode::Lot;
}

float LotPickStrategy::PickRadiusMeters() const {
    return kPickRadiusMeters;
}

std::optional<ScenePickResult> LotPickStrategy::Pick(const ScenePickContext& context) {
    if (!context.city) {
        return std::nullopt;
    }

    cISC4LotManager* lotManager = context.city->GetLotManager();
    if (!lotManager) {
        return std::nullopt;
    }

    cISC4Lot* lot = lotManager->GetLot(context.cursorWorld);
    if (!lot) {
        return std::nullopt;
    }

    cISC4LotConfiguration* config = lot->GetLotConfiguration();
    if (!config) {
        return std::nullopt;
    }

    PickedLot result;
    result.lotInstanceId = config->GetID();
    result.position = context.cursorWorld;

    cRZBaseString name;
    if (config->GetName(name)) {
        result.name.assign(name.ToChar(), name.Strlen());
    }

    SC4Rect<int32_t> cellRect;
    if (lot->GetBoundingRect(cellRect)) {
        result.hasWorldRect = true;
        result.worldMinX = static_cast<float>(cellRect.topLeftX) * kCellSizeMeters;
        result.worldMinZ = static_cast<float>(cellRect.topLeftY) * kCellSizeMeters;
        result.worldMaxX = static_cast<float>(cellRect.bottomRightX + 1) * kCellSizeMeters;
        result.worldMaxZ = static_cast<float>(cellRect.bottomRightY + 1) * kCellSizeMeters;
    }

    return ScenePickResult{result};
}
