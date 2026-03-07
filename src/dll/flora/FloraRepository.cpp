#include "FloraRepository.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "../utils/Logger.h"
#include "rfl/cbor/load.hpp"

void FloraRepository::Load() {
    try {
        const auto pluginsPath = GetPluginsPath_();
        const auto cborPath = pluginsPath / "flora.cbor";

        if (!std::filesystem::exists(cborPath)) {
            LOG_WARN("Flora CBOR file not found: {}", cborPath.string());
            return;
        }

        floraItems_.clear();
        floraById_.clear();
        floraByInstanceId_.clear();
        floraFamilyNames_.clear();
        floraFamilyInfos_.clear();
        floraGroups_.clear();
        floraGroupIds_.clear();
        floraCollections_.clear();

        floraThumbnails_.Load(pluginsPath / "flora_thumbnails.bin");

        if (auto result = rfl::cbor::load<FloraCache>(cborPath.string())) {
            floraItems_       = std::move(result->floraItems);
            floraFamilyInfos_ = std::move(result->floraFamilies);
            for (const auto& family : floraFamilyInfos_) {
                if (!family.displayName.empty()) {
                    floraFamilyNames_.emplace(family.familyId.value(), family.displayName);
                }
            }
            RebuildIndexes_();
            BuildFloraGroups_();
            BuildFloraCollections_();

            LOG_INFO("Loaded {} flora items and {} flora families from {}",
                     floraItems_.size(), floraFamilyNames_.size(), cborPath.string());
        }
        else {
            LOG_ERROR("Failed to load flora from CBOR file: {}", result.error().what());
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error loading flora: {}", e.what());
    }
}

const Flora* FloraRepository::FindFloraByInstanceId(const uint32_t instanceId) const {
    const auto it = floraByInstanceId_.find(instanceId);
    if (it != floraByInstanceId_.end()) {
        return it->second;
    }
    return nullptr;
}

void FloraRepository::RebuildIndexes_() {
    floraById_ = std::unordered_map<uint64_t, const Flora*>(floraItems_.size());
    floraByInstanceId_ = std::unordered_map<uint32_t, const Flora*>(floraItems_.size());
    for (const auto& f : floraItems_) {
        floraById_.emplace((static_cast<uint64_t>(f.groupId.value()) << 32) | f.instanceId.value(), &f);
        floraByInstanceId_.try_emplace(f.instanceId.value(), &f);
    }
}

void FloraRepository::BuildFloraGroups_() {
    floraGroups_.clear();
    floraGroupIds_.clear();

    // Find all items that are referenced as clusterNextType (i.e., are not a chain head)
    std::unordered_set<uint32_t> referencedIds;
    for (const auto& f : floraItems_) {
        if (f.clusterNextType.has_value()) {
            referencedIds.insert(f.clusterNextType->value());
        }
    }

    // Each chain head (not referenced by anything) starts a group
    for (const auto& head : floraItems_) {
        if (!head.clusterNextType.has_value()) {
            continue; // Not part of a multi-stage chain at all
        }
        if (referencedIds.contains(head.instanceId.value())) {
            continue; // Not a chain head; it's a middle or end stage
        }

        // Walk the chain from this head
        PropFamily group;
        const Flora* current = &head;
        std::unordered_set<uint32_t> visited;
        while (current != nullptr && !visited.contains(current->instanceId.value())) {
            visited.insert(current->instanceId.value());
            group.entries.push_back(FamilyEntry{current->instanceId, 1.0f});

            if (!current->clusterNextType.has_value()) {
                break;
            }
            current = FindFloraByInstanceId(current->clusterNextType->value());
        }

        if (group.entries.size() >= 2) {
            group.name = head.visibleName.empty() ? head.exemplarName : head.visibleName;
            if (group.name.empty()) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Flora Group 0x%08X", head.instanceId.value());
                group.name = buf;
            }
            floraGroupIds_.push_back(head.instanceId.value());
            floraGroups_.push_back(std::move(group));
        }
    }

    LOG_INFO("Built {} flora groups from cluster chains", floraGroups_.size());
}

void FloraRepository::BuildFloraCollections_() {
    floraCollections_.clear();

    std::unordered_map<uint32_t, std::vector<const Flora*>> familyMembers;
    for (const auto& flora : floraItems_) {
        for (const auto& familyId : flora.familyIds) {
            familyMembers[familyId.value()].push_back(&flora);
        }
    }

    std::vector<uint32_t> familyIds;
    familyIds.reserve(familyMembers.size());
    for (const auto& [familyId, _] : familyMembers) {
        familyIds.push_back(familyId);
    }
    std::ranges::sort(familyIds, [this](const uint32_t lhs, const uint32_t rhs) {
        const std::string lhsName = floraFamilyNames_.contains(lhs) ? floraFamilyNames_.at(lhs) : "";
        const std::string rhsName = floraFamilyNames_.contains(rhs) ? floraFamilyNames_.at(rhs) : "";
        if (lhsName != rhsName) {
            return lhsName < rhsName;
        }
        return lhs < rhs;
    });

    floraCollections_.reserve(familyIds.size() + floraGroups_.size());
    for (const uint32_t familyId : familyIds) {
        auto it = familyMembers.find(familyId);
        if (it == familyMembers.end() || it->second.empty()) {
            continue;
        }

        FloraCollection collection;
        collection.type = CollectionType::Family;
        collection.id = familyId;
        if (const auto nameIt = floraFamilyNames_.find(familyId); nameIt != floraFamilyNames_.end() && !nameIt->second.empty()) {
            collection.name = nameIt->second;
        }
        else {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "Family 0x%08X", familyId);
            collection.name = buffer;
        }
        collection.members = it->second;
        collection.palette.reserve(collection.members.size());
        for (const Flora* flora : collection.members) {
            collection.palette.push_back(FamilyEntry{flora->instanceId, 1.0f});
        }
        floraCollections_.push_back(std::move(collection));
    }

    for (size_t i = 0; i < floraGroups_.size(); ++i) {
        FloraCollection collection;
        collection.type = CollectionType::MultiStage;
        collection.id = i < floraGroupIds_.size() ? floraGroupIds_[i] : 0;
        collection.name = floraGroups_[i].name;
        collection.palette = floraGroups_[i].entries;
        collection.members.reserve(floraGroups_[i].entries.size());
        for (const auto& entry : floraGroups_[i].entries) {
            if (const Flora* flora = FindFloraByInstanceId(entry.propID.value())) {
                collection.members.push_back(flora);
            }
        }
        floraCollections_.push_back(std::move(collection));
    }

    LOG_INFO("Built {} flora collections", floraCollections_.size());
}

std::filesystem::path FloraRepository::GetPluginsPath_() {
    try {
        const auto modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());
        return std::filesystem::path(modulePath.get()).parent_path();
    }
    catch (const wil::ResultException& e) {
        LOG_ERROR("FloraRepository: Failed to get DLL directory: {}", e.what());
        return {};
    }
}
