#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../shared/entities.hpp"
#include "../thumbnail/ThumbnailStore.hpp"

class FloraRepository {
public:
    enum class CollectionType : uint8_t {
        Family = 0,
        MultiStage = 1
    };

    struct FloraCollection {
        CollectionType type{CollectionType::Family};
        uint32_t id{0};
        std::string name;
        std::vector<const Flora*> members;
        std::vector<FamilyEntry> palette;
    };

    void Load();

    [[nodiscard]] const std::vector<Flora>& GetFloraItems() const { return floraItems_; }
    [[nodiscard]] const std::unordered_map<uint32_t, std::string>& GetFloraFamilyNames() const { return floraFamilyNames_; }
    [[nodiscard]] const std::vector<PropFamily>& GetFloraGroups() const { return floraGroups_; }
    [[nodiscard]] const std::vector<uint32_t>& GetFloraGroupIds() const { return floraGroupIds_; }
    [[nodiscard]] const std::vector<FloraCollection>& GetFloraCollections() const { return floraCollections_; }
    [[nodiscard]] const Flora* FindFloraByInstanceId(uint32_t instanceId) const;
    [[nodiscard]] ThumbnailStore& GetFloraThumbnailStore() { return floraThumbnails_; }

private:
    void RebuildIndexes_();
    void BuildFloraGroups_();
    void BuildFloraCollections_();
    static std::filesystem::path GetPluginsPath_();

    std::vector<Flora> floraItems_;
    std::unordered_map<uint64_t, const Flora*> floraById_;
    std::unordered_map<uint32_t, const Flora*> floraByInstanceId_;
    std::unordered_map<uint32_t, std::string> floraFamilyNames_;
    std::vector<PropFamilyInfo> floraFamilyInfos_;
    // Auto-groups built from MMP cluster chains (clusterNextType chains)
    std::vector<PropFamily> floraGroups_;
    std::vector<uint32_t> floraGroupIds_;
    std::vector<FloraCollection> floraCollections_;
    ThumbnailStore floraThumbnails_;
};
