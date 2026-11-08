#include "PackFilter.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

#include "imgui.h"

void PackFilter::Begin(const PackCatalogRepository* const catalog) {
    catalog_ = catalog;
    counts_.assign((catalog ? catalog->Packs().size() : 0) + 1, 0);
    total_ = 0;
    byName_.clear();
    sorted_ = false;
}

void PackFilter::Count(const std::span<const PackCatalogRepository::ExemplarRow> rows) {
    ++total_;
    if (rows.empty()) {
        ++counts_.back();
        return;
    }
    // Once in each pack, even when a pack has it in several files.
    for (size_t i = 0; i < rows.size(); ++i) {
        const uint32_t pack = rows[i][3];
        const bool seen = std::any_of(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(i),
                                      [&](const auto& row) { return row[3] == pack; });
        if (!seen && pack + 1 < counts_.size()) {
            ++counts_[pack];
        }
    }
}

bool PackFilter::Render(const char* const id, const char* const itemsName) {
    if (!catalog_ || catalog_->Packs().empty() || counts_.empty()) {
        return false;
    }
    const std::vector<TexturePack>& packs = catalog_->Packs();
    if (!sorted_) {
        for (int32_t i = 0; i + 1 < static_cast<int32_t>(counts_.size()); ++i) {
            if (counts_[static_cast<size_t>(i)] > 0) {
                byName_.push_back(i);
            }
        }
        std::ranges::sort(byName_, {}, [&](const int32_t i) { return packs[static_cast<size_t>(i)].name; });
        sorted_ = true;
    }

    const auto label = [&](const int32_t pack) {
        char text[160];
        if (pack == kAllPacks) {
            std::snprintf(text, sizeof(text), "All packs (%zu)", total_);
        }
        else if (pack == kNoPack) {
            std::snprintf(text, sizeof(text), "Not in the catalog (%u)", counts_.back());
        }
        else {
            std::snprintf(text, sizeof(text), "%s (%u)", packs[static_cast<size_t>(pack)].name.c_str(),
                          counts_[static_cast<size_t>(pack)]);
        }
        return std::string(text);
    };

    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    if (!ImGui::BeginCombo(id, label(selected_).c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show only %s from one pack, as the SC4 Prop Texture Catalog knows them.", itemsName);
        }
        return false;
    }
    const auto option = [&](const int32_t pack) {
        if (ImGui::Selectable(label(pack).c_str(), selected_ == pack)) {
            changed = selected_ != pack;
            selected_ = pack;
        }
        if (pack >= 0 && ImGui::IsItemHovered()) {
            const TexturePack& info = packs[static_cast<size_t>(pack)];
            ImGui::SetTooltip("%s\n%s%s%s", info.author.c_str(), info.package.c_str(), info.package.empty() ? "" : "\n",
                              info.url.c_str());
        }
    };
    option(kAllPacks);
    for (const int32_t pack : byName_) {
        option(pack);
    }
    if (counts_.back() > 0) {
        option(kNoPack);
    }
    ImGui::EndCombo();
    return changed;
}

bool PackFilter::Passes(const std::span<const PackCatalogRepository::ExemplarRow> rows) const {
    if (selected_ == kAllPacks) {
        return true;
    }
    if (selected_ == kNoPack) {
        return rows.empty();
    }
    return std::ranges::any_of(rows, [&](const auto& row) { return static_cast<int32_t>(row[3]) == selected_; });
}

bool PackFilter::PackNameContains(const std::span<const PackCatalogRepository::ExemplarRow> rows,
                                  const std::string_view text) const {
    if (!catalog_ || text.empty()) {
        return false;
    }
    const auto lower = [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::ranges::any_of(rows, [&](const auto& row) {
        const TexturePack* pack = catalog_->Pack(static_cast<int32_t>(row[3]));
        return pack && !std::ranges::search(pack->name, text, {}, lower, lower).empty();
    });
}
