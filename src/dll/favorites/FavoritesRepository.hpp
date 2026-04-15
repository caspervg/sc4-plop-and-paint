#pragma once
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "../../shared/entities.hpp"

class PropRepository;

class FavoritesRepository {
public:
    explicit FavoritesRepository(const PropRepository& props);

    void Load();
    void Save() const;
    [[nodiscard]] const std::vector<RecentPaintEntryData>& GetRecentPaintsData() const;
    void SetRecentPaintsData(std::vector<RecentPaintEntryData> data);

    // Lot favorites
    [[nodiscard]] bool IsLotFavorite(uint32_t lotInstanceId) const;
    [[nodiscard]] const std::unordered_set<uint32_t>& GetFavoriteLotIds() const;
    void ToggleLotFavorite(uint32_t lotInstanceId);

    // Prop favorites
    [[nodiscard]] bool IsPropFavorite(uint32_t groupId, uint32_t instanceId) const;
    [[nodiscard]] const std::unordered_set<uint64_t>& GetFavoritePropIds() const;
    void TogglePropFavorite(uint32_t groupId, uint32_t instanceId);

    // Flora favorites
    [[nodiscard]] bool IsFloraFavorite(uint32_t groupId, uint32_t instanceId) const;
    [[nodiscard]] const std::unordered_set<uint64_t>& GetFavoriteFloraIds() const;
    void ToggleFloraFavorite(uint32_t groupId, uint32_t instanceId);

    // User-created families (formerly "palettes")
    [[nodiscard]] const std::vector<PropFamily>& GetUserFamilies() const;
    std::vector<PropFamily>& GetUserFamilies();
    [[nodiscard]] size_t GetActiveUserFamilyIndex() const;
    void SetActiveUserFamilyIndex(size_t index);
    [[nodiscard]] const PropFamily* GetActiveUserFamily() const;

    bool CreateUserFamily(const std::string& name);
    bool DeleteUserFamily(size_t index);
    bool RenameUserFamily(size_t index, const std::string& newName);
    bool AddPropToUserFamily(uint32_t propID, size_t index);
    void AddPropToNewUserFamily(uint32_t propID, const std::string& baseName);
    bool AddPropFamilyToNewUserFamily(uint32_t familyID);

private:
    static std::filesystem::path GetPluginsPath_();
    static std::string BuildDefaultFamilyName_(const std::string& baseName);
    [[nodiscard]] uint64_t GenerateNextUserFamilyId_() const;

    const PropRepository& props_;
    std::unordered_set<uint32_t> favoriteLotIds_;
    std::unordered_set<uint64_t> favoritePropIds_;
    std::unordered_set<uint64_t> favoriteFloraIds_;
    std::vector<PropFamily> userFamilies_;
    std::vector<RecentPaintEntryData> recentPaintsCache_;
    size_t activeUserFamilyIndex_{0};
};
