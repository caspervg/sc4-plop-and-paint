#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "../packs/PackCatalogRepository.hpp"
#include "DecalTextureLoader.hpp"

class cIGZPersistResourceManager;

class DecalRepository {
public:
    // packs: where textures first shipped; may be null.
    explicit DecalRepository(const PackCatalogRepository* packs = nullptr) : packs_(packs) {}

    // Discovers all zoom-level-4 texture instances of type 0x7AB50E44 / group 0x0986135E.
    // Call on PostCityInit when the PersistResourceManager is fully populated.
    void Populate(cIGZPersistResourceManager* pRM);

    void Clear();

    [[nodiscard]] const std::vector<uint32_t>& GetInstanceIds() const { return instanceIds_; }
    [[nodiscard]] size_t Count() const { return instanceIds_.size(); }

    // instanceIds_ is kept sorted by Populate.
    [[nodiscard]] bool Contains(const uint32_t instanceId) const {
        return std::binary_search(instanceIds_.begin(), instanceIds_.end(), instanceId);
    }

    // Every pack and file the catalog has the texture in: rows of instance, index into files, index
    // into Packs(). Cheap; no file lookups.
    [[nodiscard]] std::span<const PackCatalogRepository::TextureRow> PackCandidates(uint32_t instanceId) const;

    // Where a texture shipped, from the candidate that matches the file the game loads it from
    // (an sc4pac folder or the original file name), else the first; nullptr when the catalog does
    // not know it. With several candidates the first call looks the file up, so call these for
    // textures on screen, not for the whole library at once.
    [[nodiscard]] const std::string* OriginalFile(uint32_t instanceId) const;
    [[nodiscard]] const TexturePack* PackOf(uint32_t instanceId) const;
    // Index into Packs(), or -1.
    [[nodiscard]] int32_t PackIndexOf(uint32_t instanceId) const;
    [[nodiscard]] const std::vector<TexturePack>& Packs() const;

    // Base (opaque) or overlay (has transparency). Unknown until ClassifySome got to it.
    [[nodiscard]] decals::TextureKind KindOf(uint32_t instanceId) const;
    // Reads textures not classified yet for up to the budget, in ID order. True when all are done.
    // Every texture is read from its file, so this is spread over frames, and only done on demand.
    bool ClassifySome(std::chrono::microseconds budget);
    [[nodiscard]] size_t ClassifiedCount() const { return classified_; }

private:
    [[nodiscard]] const PackCatalogRepository::TextureRow* Preferred_(uint32_t instanceId) const;

    std::vector<uint32_t> instanceIds_;
    // By the place of the texture in instanceIds_.
    std::vector<decals::TextureKind> kinds_;
    size_t classified_ = 0;
    const PackCatalogRepository* packs_ = nullptr;
    cIGZPersistResourceManager* pRM_ = nullptr;
    // The chosen candidate of textures with several, by its place among them, once looked up.
    mutable std::unordered_map<uint32_t, uint32_t> preferred_;
};
