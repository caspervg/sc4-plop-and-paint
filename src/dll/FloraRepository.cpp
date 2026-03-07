#include "FloraRepository.hpp"

#include <algorithm>
#include <unordered_set>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "Utils.hpp"
#include "rfl/cbor/load.hpp"
#include "utils/Logger.h"

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
