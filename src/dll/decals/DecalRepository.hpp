#pragma once
#include <cstdint>
#include <vector>

class cIGZPersistResourceManager;

class DecalRepository {
public:
    DecalRepository() = default;

    // Discovers all zoom-level-4 texture instances of type 0x7AB50E44 / group 0x0986135E.
    // Call on PostCityInit when the PersistResourceManager is fully populated.
    void Populate(cIGZPersistResourceManager* pRM);

    void Clear();

    [[nodiscard]] const std::vector<uint32_t>& GetInstanceIds() const { return instanceIds_; }
    [[nodiscard]] size_t Count() const { return instanceIds_.size(); }

private:
    std::vector<uint32_t> instanceIds_;
};
