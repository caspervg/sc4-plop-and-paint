#include "DecalRepository.hpp"

#include <algorithm>
#include <cctype>

#include "cGZPersistResourceKey.h"
#include "cIGZPersistResourceKeyList.h"
#include "cIGZPersistDBSegment.h"
#include "cIGZPersistResourceManager.h"
#include "cRZAutoRefCount.h"
#include "cRZBaseString.h"
#include "../utils/Logger.h"

namespace {
    constexpr uint32_t kDecalTextureType  = 0x7AB50E44;
    constexpr uint32_t kDecalTextureGroup = 0x0986135E;
    constexpr uint32_t kZoom4Mask         = 0xF;
    constexpr uint32_t kZoom4Value        = 4;
}

void DecalRepository::Populate(cIGZPersistResourceManager* pRM) {
    instanceIds_.clear();
    preferred_.clear();
    kinds_.clear();
    classified_ = 0;
    pRM_ = pRM;
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
        const uint32_t zoomVariant = key.instance & kZoom4Mask;
        if (key.group == kDecalTextureGroup &&
            (zoomVariant == kZoom4Value || zoomVariant == 9 || zoomVariant == 0xE)) {
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

    kinds_.assign(instanceIds_.size(), decals::TextureKind::Unknown);
    LOG_INFO("DecalRepository: found {} unique zoom-4 decal textures", instanceIds_.size());
}

void DecalRepository::Clear() {
    instanceIds_.clear();
    preferred_.clear();
    kinds_.clear();
    classified_ = 0;
    pRM_ = nullptr;
}

std::span<const PackCatalogRepository::TextureRow> DecalRepository::PackCandidates(const uint32_t instanceId) const {
    return packs_ ? packs_->TextureRows(instanceId) : std::span<const PackCatalogRepository::TextureRow>{};
}

const PackCatalogRepository::TextureRow* DecalRepository::Preferred_(const uint32_t instanceId) const {
    const auto candidates = PackCandidates(instanceId);
    if (candidates.size() <= 1) {
        return candidates.empty() ? nullptr : &candidates.front();
    }
    if (const auto it = preferred_.find(instanceId); it != preferred_.end()) {
        return &candidates[it->second];
    }

    // The file the game loads the texture from. sc4pac installs into folders named
    // "<group>.<name>.<version>.sc4pac", and the catalog names packages "<group>:<name>".
    uint32_t chosen = 0;
    cRZAutoRefCount<cIGZPersistDBSegment> segment;
    const cGZPersistResourceKey key{kDecalTextureType, kDecalTextureGroup, instanceId};
    if (pRM_ && pRM_->FindDBSegment(key, segment.AsPPObj()) && segment) {
        cRZBaseString text;
        segment->GetPath(text);
        std::string path(text.ToChar(), text.Strlen());
        std::ranges::transform(path, path.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto lower = [](std::string value) {
            std::ranges::transform(value, value.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        };

        bool found = false;
        for (uint32_t i = 0; i < candidates.size() && !found; ++i) {
            const TexturePack* pack = packs_->Pack(static_cast<int32_t>(candidates[i][2]));
            if (pack && !pack->package.empty()) {
                std::string folder = lower(pack->package);
                std::ranges::replace(folder, ':', '.');
                found = path.find(folder + '.') != std::string::npos;
                chosen = found ? i : chosen;
            }
        }
        // Not installed by sc4pac: the original file, if it is still the one loaded.
        for (uint32_t i = 0; i < candidates.size() && !found; ++i) {
            const std::string* file = packs_->File(candidates[i][1]);
            found = file && path.find(lower(*file) + '.') != std::string::npos;
            chosen = found ? i : chosen;
        }
    }
    preferred_.emplace(instanceId, chosen);
    return &candidates[chosen];
}

const std::string* DecalRepository::OriginalFile(const uint32_t instanceId) const {
    const auto* row = Preferred_(instanceId);
    return row ? packs_->File((*row)[1]) : nullptr;
}

const TexturePack* DecalRepository::PackOf(const uint32_t instanceId) const {
    const auto* row = Preferred_(instanceId);
    return row ? packs_->Pack(static_cast<int32_t>((*row)[2])) : nullptr;
}

int32_t DecalRepository::PackIndexOf(const uint32_t instanceId) const {
    const auto* row = Preferred_(instanceId);
    return row ? static_cast<int32_t>((*row)[2]) : -1;
}

const std::vector<TexturePack>& DecalRepository::Packs() const {
    static const std::vector<TexturePack> kNone;
    return packs_ ? packs_->Packs() : kNone;
}

decals::TextureKind DecalRepository::KindOf(const uint32_t instanceId) const {
    const auto it = std::lower_bound(instanceIds_.begin(), instanceIds_.end(), instanceId);
    return it != instanceIds_.end() && *it == instanceId ? kinds_[static_cast<size_t>(it - instanceIds_.begin())]
                                                         : decals::TextureKind::Unknown;
}

bool DecalRepository::ClassifySome(const std::chrono::microseconds budget) {
    // ponytail: classified again every session (a few seconds for thousands of textures); keep them
    // in a cache file if that gets slow.
    const auto end = std::chrono::steady_clock::now() + budget;
    while (classified_ < instanceIds_.size() && std::chrono::steady_clock::now() < end) {
        kinds_[classified_] = decals::ClassifyDecalTexture(pRM_, instanceIds_[classified_]);
        ++classified_;
    }
    return classified_ == instanceIds_.size();
}
