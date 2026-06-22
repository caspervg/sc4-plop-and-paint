#include "BuildingStyleService.hpp"

#include <algorithm>
#include <format>

#include "../utils/Logger.h"
#include "SC4Vector.h"
#include "cIGZCOM.h"
#include "cISC4City.h"
#include "cISC4TractDeveloper.h"
#include "cRZBaseString.h"

bool BuildingStyleService::Acquire(cIGZCOM* com) {
    Shutdown();
    if (!com) {
        return false;
    }

    if (!com->GetClassObject(GZCLSID_cIBuildingStyleInfo, GZIID_cIBuildingStyleInfo2, styleInfo2_.AsPPVoid())) {
        com->GetClassObject(GZCLSID_cIBuildingStyleInfo, GZIID_cIBuildingStyleInfo, styleInfo1_.AsPPVoid());
    }

    com->GetClassObject(GZCLSID_cIBuildingStyleWallToWall, GZIID_cIBuildingStyleWallToWall, wallToWallInfo_.AsPPVoid());

    RefreshWallToWallGroups_();

    if (GetStyleInfo_()) {
        LOG_INFO("Acquired More Building Styles information API");
    }
    else {
        LOG_INFO("More Building Styles information API not available; using cache-derived style labels");
    }

    return GetStyleInfo_() != nullptr || wallToWallInfo_ != nullptr;
}

void BuildingStyleService::RefreshForCity(cISC4City* city) {
    RefreshAvailableStyles_();
    RefreshActiveStyles(city);
}

void BuildingStyleService::RefreshActiveStyles(cISC4City* city) {
    context_.activeStyleIds.clear();
    context_.activeStylesKnown = false;
    context_.useAllStylesAtOnce = false;

    if (!city) {
        return;
    }

    if (const auto* tractDeveloper = city->GetTractDeveloper()) {
        const auto& activeStyles = tractDeveloper->GetActiveStyles();
        context_.activeStyleIds.insert(activeStyles.begin(), activeStyles.end());
        context_.useAllStylesAtOnce = tractDeveloper->IsUsingAllStylesAtOnce();
        context_.activeStylesKnown = true;
    }
}

void BuildingStyleService::ClearCityState() {
    context_.availableStyleIds.clear();
    context_.availabilityKnown = false;
    context_.activeStyleIds.clear();
    context_.activeStylesKnown = false;
    context_.useAllStylesAtOnce = false;
    availableStyles_.clear();
}

void BuildingStyleService::Shutdown() {
    ClearCityState();
    context_.wallToWallOccupantGroups.clear();
    wallToWallInfo_.Reset();
    styleInfo2_.Reset();
    styleInfo1_.Reset();
}

void BuildingStyleService::RefreshAvailableStyles_() {
    context_.availableStyleIds.clear();
    context_.availabilityKnown = false;
    availableStyles_.clear();

    cIBuildingStyleInfo* styleInfo = GetStyleInfo_();
    if (!styleInfo) {
        return;
    }

    const uint32_t count = styleInfo->GetAvailableBuildingStyleIds(nullptr, 0);
    if (count == 0) {
        return;
    }

    std::vector<uint32_t> ids(count);
    const uint32_t copied = styleInfo->GetAvailableBuildingStyleIds(ids.data(), count);
    ids.resize(std::min(count, copied));

    availableStyles_.reserve(ids.size());
    for (const uint32_t id : ids) {
        cRZBaseString name;
        if (styleInfo->GetBuildingStyleName(id, name) && name.Strlen() > 0) {
            availableStyles_.push_back({id, std::string(name.ToChar(), name.Strlen())});
        }
        else {
            availableStyles_.push_back({id, std::format("Style 0x{:X}", id)});
        }
        context_.availableStyleIds.insert(id);
    }

    std::ranges::sort(availableStyles_,
                      [](const BuildingStyleEntry& lhs, const BuildingStyleEntry& rhs) { return lhs.name < rhs.name; });
    context_.availabilityKnown = true;
}

void BuildingStyleService::RefreshWallToWallGroups_() {
    context_.wallToWallOccupantGroups.clear();
    if (!wallToWallInfo_) {
        return;
    }

    const uint32_t count = wallToWallInfo_->GetWallToWallOccupantGroupIds(nullptr, 0);
    if (count == 0) {
        return;
    }

    std::vector<uint32_t> groups(count);
    const uint32_t copied = wallToWallInfo_->GetWallToWallOccupantGroupIds(groups.data(), count);
    groups.resize(std::min(count, copied));
    context_.wallToWallOccupantGroups.insert(groups.begin(), groups.end());
}

cIBuildingStyleInfo* BuildingStyleService::GetStyleInfo_() const {
    if (styleInfo2_) {
        return static_cast<cIBuildingStyleInfo*>(styleInfo2_);
    }
    return styleInfo1_;
}
