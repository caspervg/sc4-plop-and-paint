#include "FavoritesRepository.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "../common/Utils.hpp"
#include "../props/PropRepository.hpp"
#include "../utils/Logger.h"
#include "rfl/cbor/load.hpp"
#include "rfl/cbor/save.hpp"

FavoritesRepository::FavoritesRepository(const PropRepository& props)
    : props_(props) {}

void FavoritesRepository::Load() {
    favoriteLotIds_.clear();
    favoritePropIds_.clear();
    favoriteFloraIds_.clear();
    userFamilies_.clear();
    recentPaintsCache_.clear();
    activeUserFamilyIndex_ = 0;

    try {
        const auto pluginsPath = GetPluginsPath_();
        const auto cborPath = pluginsPath / "favorites.cbor";

        if (!std::filesystem::exists(cborPath)) {
            LOG_INFO("Favorites file not found, starting with empty favorites");
            return;
        }

        if (auto result = rfl::cbor::load<AllFavorites>(cborPath.string())) {
            for (const auto& hexId : result->lots.items) {
                favoriteLotIds_.insert(static_cast<uint32_t>(hexId.value()));
            }
            if (result->props) {
                for (const auto& hexId : result->props->items) {
                    favoritePropIds_.insert(hexId.value());
                }
            }
            if (result->flora) {
                for (const auto& hexId : result->flora->items) {
                    favoriteFloraIds_.insert(hexId.value());
                }
            }

            if (result->families) {
                userFamilies_ = *result->families;
                for (auto& family : userFamilies_) {
                    family.densityVariation = std::clamp(family.densityVariation, 0.0f, 1.0f);
                    if (!family.persistentId.has_value()) {
                        family.persistentId = rfl::Hex<uint64_t>(GenerateNextUserFamilyId_());
                    }
                    std::erase_if(family.entries, [this](const FamilyEntry& entry) {
                        return props_.FindPropByInstanceId(entry.propID.value()) == nullptr;
                    });
                    for (auto& entry : family.entries) {
                        entry.weight = std::max(0.1f, entry.weight);
                    }
                }
            }
            if (result->recentPaints) {
                recentPaintsCache_ = *result->recentPaints;
            }

            LOG_INFO("Loaded {} favorite lots, {} user families from {}",
                     favoriteLotIds_.size(), userFamilies_.size(), cborPath.string());
        }
        else {
            LOG_WARN("Failed to load favorites from CBOR file: {}", result.error().what());
        }
    }
    catch (const std::exception& e) {
        LOG_WARN("Error loading favorites (will start empty): {}", e.what());
    }
}

void FavoritesRepository::Save() const {
    try {
        const auto pluginsPath = GetPluginsPath_();
        const auto cborPath = pluginsPath / "favorites.cbor";

        AllFavorites allFavorites;
        allFavorites.version = 4;

        for (const uint32_t id : favoriteLotIds_) {
            allFavorites.lots.items.emplace_back(id);
        }

        const auto now = std::chrono::system_clock::now();
        const auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
        localtime_s(&tm_now, &time_t_now);
        allFavorites.lastModified = rfl::Timestamp<"%Y-%m-%dT%H:%M:%S">(tm_now);

        if (!favoritePropIds_.empty()) {
            TabFavorites propFavorites;
            propFavorites.items.reserve(favoritePropIds_.size());
            for (const uint64_t id : favoritePropIds_) {
                propFavorites.items.emplace_back(id);
            }
            allFavorites.props = std::move(propFavorites);
        }
        if (!favoriteFloraIds_.empty()) {
            TabFavorites floraFavorites;
            floraFavorites.items.reserve(favoriteFloraIds_.size());
            for (const uint64_t id : favoriteFloraIds_) {
                floraFavorites.items.emplace_back(id);
            }
            allFavorites.flora = std::move(floraFavorites);
        }
        else {
            allFavorites.flora = std::nullopt;
        }
        allFavorites.families = userFamilies_.empty() ? std::nullopt : std::make_optional(userFamilies_);
        allFavorites.recentPaints = recentPaintsCache_.empty()
            ? std::nullopt
            : std::make_optional(recentPaintsCache_);

        if (const auto saveResult = rfl::cbor::save(cborPath.string(), allFavorites)) {
            LOG_INFO("Saved {} favorite lots, {} user families to {}",
                     favoriteLotIds_.size(), userFamilies_.size(), cborPath.string());
        }
        else {
            LOG_ERROR("Failed to save favorites: {}", saveResult.error().what());
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error saving favorites: {}", e.what());
    }
}

const std::vector<RecentPaintEntryData>& FavoritesRepository::GetRecentPaintsData() const {
    return recentPaintsCache_;
}

void FavoritesRepository::SetRecentPaintsData(std::vector<RecentPaintEntryData> data) {
    recentPaintsCache_ = std::move(data);
}

bool FavoritesRepository::IsLotFavorite(const uint32_t lotInstanceId) const {
    return favoriteLotIds_.contains(lotInstanceId);
}

const std::unordered_set<uint32_t>& FavoritesRepository::GetFavoriteLotIds() const {
    return favoriteLotIds_;
}

void FavoritesRepository::ToggleLotFavorite(const uint32_t lotInstanceId) {
    if (favoriteLotIds_.contains(lotInstanceId)) {
        favoriteLotIds_.erase(lotInstanceId);
        LOG_INFO("Removed lot favorite: 0x{:08X}", lotInstanceId);
    }
    else {
        favoriteLotIds_.insert(lotInstanceId);
        LOG_INFO("Added lot favorite: 0x{:08X}", lotInstanceId);
    }
    Save();
}

bool FavoritesRepository::IsPropFavorite(const uint32_t groupId, const uint32_t instanceId) const {
    return favoritePropIds_.contains(MakeGIKey(groupId, instanceId));
}

const std::unordered_set<uint64_t>& FavoritesRepository::GetFavoritePropIds() const {
    return favoritePropIds_;
}

void FavoritesRepository::TogglePropFavorite(const uint32_t groupId, const uint32_t instanceId) {
    const uint64_t key = MakeGIKey(groupId, instanceId);
    if (favoritePropIds_.contains(key)) {
        favoritePropIds_.erase(key);
        LOG_INFO("Removed prop favorite: 0x{:08X}/0x{:08X}", groupId, instanceId);
    }
    else {
        favoritePropIds_.insert(key);
        LOG_INFO("Added prop favorite: 0x{:08X}/0x{:08X}", groupId, instanceId);
    }
    Save();
}

bool FavoritesRepository::IsFloraFavorite(const uint32_t groupId, const uint32_t instanceId) const {
    return favoriteFloraIds_.contains(MakeGIKey(groupId, instanceId));
}

const std::unordered_set<uint64_t>& FavoritesRepository::GetFavoriteFloraIds() const {
    return favoriteFloraIds_;
}

void FavoritesRepository::ToggleFloraFavorite(const uint32_t groupId, const uint32_t instanceId) {
    const uint64_t key = MakeGIKey(groupId, instanceId);
    if (favoriteFloraIds_.contains(key)) {
        favoriteFloraIds_.erase(key);
        LOG_INFO("Removed flora favorite: 0x{:08X}/0x{:08X}", groupId, instanceId);
    }
    else {
        favoriteFloraIds_.insert(key);
        LOG_INFO("Added flora favorite: 0x{:08X}/0x{:08X}", groupId, instanceId);
    }
    Save();
}

const std::vector<PropFamily>& FavoritesRepository::GetUserFamilies() const {
    return userFamilies_;
}

std::vector<PropFamily>& FavoritesRepository::GetUserFamilies() {
    return userFamilies_;
}

size_t FavoritesRepository::GetActiveUserFamilyIndex() const {
    return activeUserFamilyIndex_;
}

void FavoritesRepository::SetActiveUserFamilyIndex(const size_t index) {
    if (userFamilies_.empty()) {
        activeUserFamilyIndex_ = 0;
        return;
    }
    activeUserFamilyIndex_ = std::min(index, userFamilies_.size() - 1);
}

const PropFamily* FavoritesRepository::GetActiveUserFamily() const {
    if (userFamilies_.empty() || activeUserFamilyIndex_ >= userFamilies_.size()) {
        return nullptr;
    }
    return &userFamilies_[activeUserFamilyIndex_];
}

bool FavoritesRepository::CreateUserFamily(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const auto& family : userFamilies_) {
        if (family.name == name) {
            return false;
        }
    }
    PropFamily family;
    family.name = name;
    family.persistentId = rfl::Hex<uint64_t>(GenerateNextUserFamilyId_());
    userFamilies_.push_back(std::move(family));
    activeUserFamilyIndex_ = userFamilies_.size() - 1;
    Save();
    return true;
}

bool FavoritesRepository::DeleteUserFamily(const size_t index) {
    if (index >= userFamilies_.size()) {
        return false;
    }
    userFamilies_.erase(userFamilies_.begin() + static_cast<std::ptrdiff_t>(index));
    if (userFamilies_.empty()) {
        activeUserFamilyIndex_ = 0;
    }
    else {
        activeUserFamilyIndex_ = std::min(activeUserFamilyIndex_, userFamilies_.size() - 1);
    }
    Save();
    return true;
}

bool FavoritesRepository::RenameUserFamily(const size_t index, const std::string& newName) {
    if (index >= userFamilies_.size() || newName.empty()) {
        return false;
    }
    for (size_t i = 0; i < userFamilies_.size(); ++i) {
        if (i != index && userFamilies_[i].name == newName) {
            return false;
        }
    }
    userFamilies_[index].name = newName;
    Save();
    return true;
}

bool FavoritesRepository::AddPropToUserFamily(const uint32_t propID, const size_t index) {
    if (index >= userFamilies_.size() || propID == 0) {
        return false;
    }
    if (!props_.FindPropByInstanceId(propID)) {
        LOG_WARN("Cannot add prop 0x{:08X} to family: prop not found", propID);
        return false;
    }
    auto& family = userFamilies_[index];
    for (const auto& entry : family.entries) {
        if (entry.propID.value() == propID) {
            return false;
        }
    }
    family.entries.push_back(FamilyEntry{rfl::Hex<uint32_t>(propID), 1.0f});
    activeUserFamilyIndex_ = index;
    Save();
    return true;
}

void FavoritesRepository::AddPropToNewUserFamily(const uint32_t propID, const std::string& baseName) {
    const std::string defaultName = BuildDefaultFamilyName_(baseName);
    std::string candidateName = defaultName;
    int suffix = 2;

    while (std::any_of(userFamilies_.begin(), userFamilies_.end(), [&](const PropFamily& f) {
        return f.name == candidateName;
    })) {
        candidateName = defaultName + " (" + std::to_string(suffix++) + ")";
    }

    if (!CreateUserFamily(candidateName)) {
        return;
    }
    AddPropToUserFamily(propID, activeUserFamilyIndex_);
}

bool FavoritesRepository::AddPropFamilyToNewUserFamily(const uint32_t familyID) {
    if (familyID == 0) {
        return false;
    }

    std::unordered_set<uint32_t> uniquePropIds;
    for (const auto& prop : props_.GetProps()) {
        if (std::any_of(prop.familyIds.begin(), prop.familyIds.end(),
                        [familyID](const rfl::Hex<uint32_t>& id) { return id.value() == familyID; })) {
            uniquePropIds.insert(prop.instanceId.value());
        }
    }

    if (uniquePropIds.empty()) {
        return false;
    }

    std::string baseName;
    const auto& familyNames = props_.GetPropFamilyNames();
    if (const auto it = familyNames.find(familyID); it != familyNames.end() && !it->second.empty()) {
        baseName = it->second;
    }
    else {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "Family 0x%08X", familyID);
        baseName = buffer;
    }

    std::string candidateName = BuildDefaultFamilyName_(baseName);
    int suffix = 2;
    while (std::any_of(userFamilies_.begin(), userFamilies_.end(), [&](const PropFamily& f) {
        return f.name == candidateName;
    })) {
        candidateName = BuildDefaultFamilyName_(baseName) + " (" + std::to_string(suffix++) + ")";
    }

    PropFamily family;
    family.name = std::move(candidateName);
    family.persistentId = rfl::Hex<uint64_t>(GenerateNextUserFamilyId_());
    family.entries.reserve(uniquePropIds.size());
    for (const uint32_t propId : uniquePropIds) {
        family.entries.push_back(FamilyEntry{rfl::Hex<uint32_t>(propId), 1.0f});
    }

    userFamilies_.push_back(std::move(family));
    activeUserFamilyIndex_ = userFamilies_.size() - 1;
    Save();
    return true;
}

std::filesystem::path FavoritesRepository::GetPluginsPath_() {
    try {
        const auto modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());
        return std::filesystem::path(modulePath.get()).parent_path();
    }
    catch (const wil::ResultException& e) {
        LOG_ERROR("FavoritesRepository: Failed to get DLL directory: {}", e.what());
        return {};
    }
}

std::string FavoritesRepository::BuildDefaultFamilyName_(const std::string& baseName) {
    std::string name = baseName.empty() ? std::string("Family") : baseName;
    name += " mix";
    return name;
}

uint64_t FavoritesRepository::GenerateNextUserFamilyId_() const {
    uint64_t nextId = 1;
    for (const auto& family : userFamilies_) {
        if (family.persistentId.has_value()) {
            nextId = std::max(nextId, family.persistentId->value() + 1);
        }
    }
    return nextId;
}
