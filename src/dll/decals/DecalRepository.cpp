#include "DecalRepository.hpp"

#include <algorithm>

#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZPersistResourceManager.h"
#include "../utils/Logger.h"

namespace {
    constexpr uint32_t kDecalTextureType  = 0x7AB50E44;
    constexpr uint32_t kDecalTextureGroup = 0x0986135E;
    constexpr uint32_t kZoom4Mask         = 0xF;
    constexpr uint32_t kZoom4Value        = 4;
}

void DecalRepository::Populate(cIGZPersistResourceManager* pRM) {
    instanceIds_.clear();
    if (!pRM) {
        LOG_WARN("DecalRepository::Populate called with null resource manager");
        return;
    }

    cIGZPersistResourceKeyList* rawList = nullptr;
    pRM->GetAvailableResourceListForType(&rawList, kDecalTextureType);
    if (!rawList) {
        LOG_INFO("DecalRepository: no texture resources of type 0x{:08X} found", kDecalTextureType);
        return;
    }

    const uint32_t count = rawList->Size();
    instanceIds_.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        const cGZPersistResourceKey& key = rawList->GetKey(i);
        if (key.group == kDecalTextureGroup && (key.instance & kZoom4Mask) == kZoom4Value) {
            instanceIds_.push_back(key.instance);
        }
    }

    rawList->Release();

    const size_t rawMatchCount = instanceIds_.size();
    std::sort(instanceIds_.begin(), instanceIds_.end());
    instanceIds_.erase(std::unique(instanceIds_.begin(), instanceIds_.end()), instanceIds_.end());

    const size_t duplicateCount = rawMatchCount - instanceIds_.size();
    if (duplicateCount > 0) {
        LOG_INFO("DecalRepository: deduplicated {} duplicate zoom-4 decal texture entries", duplicateCount);
    }

    LOG_INFO("DecalRepository: found {} unique zoom-4 decal textures", instanceIds_.size());
}

void DecalRepository::Clear() {
    instanceIds_.clear();
}
