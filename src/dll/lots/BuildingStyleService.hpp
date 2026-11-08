#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4828)
#endif
#include "cIBuildingStyleInfo.h"
#include "cIBuildingStyleInfo2.h"
#include "cIBuildingStyleWallToWall.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "cRZAutoRefCount.h"
#include "shared/BuildingStyleResolver.hpp"

class cIGZCOM;
class cISC4City;

struct BuildingStyleEntry {
    uint32_t id;
    std::string name;
};

class BuildingStyleService {
public:
    bool Acquire(cIGZCOM* com);
    void RefreshForCity(cISC4City* city);
    void RefreshActiveStyles(cISC4City* city);
    void ClearCityState();
    void Shutdown();

    [[nodiscard]] const BuildingStyleContext& GetContext() const { return context_; }
    [[nodiscard]] const std::vector<BuildingStyleEntry>& GetAvailableStyles() const { return availableStyles_; }
    [[nodiscard]] bool HasStyleInfoApi() const { return GetStyleInfo_() != nullptr; }

private:
    void RefreshAvailableStyles_();
    void RefreshWallToWallGroups_();
    [[nodiscard]] cIBuildingStyleInfo* GetStyleInfo_() const;

    cRZAutoRefCount<cIBuildingStyleInfo2> styleInfo2_;
    cRZAutoRefCount<cIBuildingStyleInfo> styleInfo1_;
    cRZAutoRefCount<cIBuildingStyleWallToWall> wallToWallInfo_;
    BuildingStyleContext context_;
    std::vector<BuildingStyleEntry> availableStyles_;
};
