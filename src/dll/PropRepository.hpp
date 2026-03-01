#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "../shared/entities.hpp"

class PropRepository {
public:
    void Load();

    [[nodiscard]] const std::vector<Prop>& GetProps() const { return props_; }
    [[nodiscard]] const std::unordered_map<uint64_t, Prop>& GetPropsById() const { return propsById_; }
    [[nodiscard]] const std::unordered_map<uint32_t, std::string>& GetPropFamilyNames() const { return propFamilyNames_; }
    [[nodiscard]] const std::vector<PropFamily>& GetAutoFamilies() const { return autoFamilies_; }
    [[nodiscard]] const Prop* FindPropByInstanceId(uint32_t instanceId) const;

private:
    void RebuildIndexes_();
    void BuildAutoFamilies_();
    static std::filesystem::path GetPluginsPath_();

    std::vector<Prop> props_;
    std::unordered_map<uint64_t, Prop> propsById_;
    std::unordered_map<uint32_t, std::string> propFamilyNames_;
    std::vector<PropFamilyInfo> propFamilyInfos_;
    std::vector<PropFamily> autoFamilies_;
};
