#include "PropRepository.hpp"

#include <algorithm>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "Utils.hpp"
#include "rfl/cbor/load.hpp"
#include "utils/Logger.h"

void PropRepository::Load() {
    try {
        const auto pluginsPath = GetPluginsPath_();
        const auto cborPath = pluginsPath / "props.cbor";

        if (!std::filesystem::exists(cborPath)) {
            LOG_WARN("Prop CBOR file not found: {}", cborPath.string());
            return;
        }

        props_.clear();
        propsById_.clear();
        propsByInstanceId_.clear();
        propFamilyNames_.clear();
        propFamilyInfos_.clear();
        autoFamilies_.clear();
        autoFamilyIds_.clear();

        propThumbnails_.Load(pluginsPath / "prop_thumbnails.bin");

        if (auto result = rfl::cbor::load<PropsCache>(cborPath.string())) {
            props_ = std::move(result->props);
            propFamilyInfos_ = std::move(result->propFamilies);
            for (const auto& family : propFamilyInfos_) {
                if (!family.displayName.empty()) {
                    propFamilyNames_.emplace(family.familyId.value(), family.displayName);
                }
            }
            RebuildIndexes_();
            BuildAutoFamilies_();

            LOG_INFO("Loaded {} props and {} prop families from {}",
                     props_.size(), propFamilyNames_.size(), cborPath.string());
        }
        else {
            LOG_ERROR("Failed to load props from CBOR file: {}", result.error().what());
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error loading props: {}", e.what());
    }
}

const Prop* PropRepository::FindPropByInstanceId(const uint32_t instanceId) const {
    const auto it = propsByInstanceId_.find(instanceId);
    if (it != propsByInstanceId_.end()) {
        return it->second;
    }

    return nullptr;
}

void PropRepository::RebuildIndexes_() {
    propsById_ = std::unordered_map<uint64_t, const Prop*>(props_.size());
    propsByInstanceId_ = std::unordered_map<uint32_t, const Prop*>(props_.size());
    for (const auto& p : props_) {
        propsById_.emplace((static_cast<uint64_t>(p.groupId.value()) << 32) | p.instanceId.value(), &p);
        propsByInstanceId_.try_emplace(p.instanceId.value(), &p);
    }
}

void PropRepository::BuildAutoFamilies_() {
    autoFamilies_.clear();
    autoFamilyIds_.clear();
    autoFamilies_.reserve(propFamilyInfos_.size());
    autoFamilyIds_.reserve(propFamilyInfos_.size());

    for (const auto& familyInfo : propFamilyInfos_) {
        const uint32_t familyId = familyInfo.familyId.value();

        PropFamily family;
        family.name = familyInfo.displayName.empty()
            ? [&]() {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "Family 0x%08X", familyId);
                return std::string(buf);
            }()
            : familyInfo.displayName;

        for (const auto& prop : props_) {
            const bool hasFamilyId = std::any_of(prop.familyIds.begin(), prop.familyIds.end(),
                [familyId](const rfl::Hex<uint32_t>& id) { return id.value() == familyId; });
            if (hasFamilyId) {
                family.entries.push_back(FamilyEntry{prop.instanceId, 1.0f});
            }
        }

        if (!family.entries.empty()) {
            autoFamilies_.push_back(std::move(family));
            autoFamilyIds_.push_back(familyId);
        }
    }
}

std::filesystem::path PropRepository::GetPluginsPath_() {
    try {
        const auto modulePath = wil::GetModuleFileNameW(wil::GetModuleInstanceHandle());
        return std::filesystem::path(modulePath.get()).parent_path();
    }
    catch (const wil::ResultException& e) {
        LOG_ERROR("PropRepository: Failed to get DLL directory: {}", e.what());
        return {};
    }
}
