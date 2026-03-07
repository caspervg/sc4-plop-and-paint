#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../shared/entities.hpp"
#include "../thumbnail/ThumbnailStore.hpp"

class PropRepository {
public:
    void Load();

    [[nodiscard]] const std::vector<Prop>& GetProps() const { return props_; }
    [[nodiscard]] const std::unordered_map<uint64_t, const Prop*>& GetPropsById() const { return propsById_; }
    [[nodiscard]] const std::unordered_map<uint32_t, std::string>& GetPropFamilyNames() const { return propFamilyNames_; }
    [[nodiscard]] const std::vector<PropFamily>& GetAutoFamilies() const { return autoFamilies_; }
    [[nodiscard]] const std::vector<uint32_t>& GetAutoFamilyIds() const { return autoFamilyIds_; }
    [[nodiscard]] const Prop* FindPropByInstanceId(uint32_t instanceId) const;
    [[nodiscard]] ThumbnailStore& GetPropThumbnailStore() { return propThumbnails_; }

private:
    void RebuildIndexes_();
    void BuildAutoFamilies_();
    static std::filesystem::path GetPluginsPath_();

    std::vector<Prop> props_;
    std::unordered_map<uint64_t, const Prop*> propsById_;
    std::unordered_map<uint32_t, const Prop*> propsByInstanceId_;
    std::unordered_map<uint32_t, std::string> propFamilyNames_;
    std::vector<PropFamilyInfo> propFamilyInfos_;
    std::vector<PropFamily> autoFamilies_;
    std::vector<uint32_t> autoFamilyIds_;
    ThumbnailStore propThumbnails_;
};
