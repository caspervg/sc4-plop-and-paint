#include "PackFilter.hpp"
#include "PackSearch.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>

#include "imgui.h"

void PackFilter::Begin(const PackCatalogRepository* const catalog) {
    catalog_ = catalog;
    counts_.assign((catalog ? catalog->Packs().size() : 0) + 1, 0);
    fileCounts_.assign(catalog ? catalog->FileCount() : 0, 0);
    filePacks_.assign(fileCounts_.size(), 0);
    total_ = 0;
    byName_.clear();
    filesByName_.clear();
    sorted_ = false;
}

// In a row, the file index and the pack index are the last two values.
template <size_t N>
void PackFilter::Count_(const std::span<const std::array<uint32_t, N>> rows) {
    ++total_;
    if (rows.empty()) {
        ++counts_.back();
        return;
    }
    // Once in each pack and file, even when a pack has it in several files.
    for (size_t i = 0; i < rows.size(); ++i) {
        const uint32_t file = rows[i][N - 2];
        const uint32_t pack = rows[i][N - 1];
        const auto earlier = rows.first(i);
        if (pack + 1 < counts_.size() && std::ranges::none_of(earlier, [&](const auto& row) { return row[N - 1] == pack; })) {
            ++counts_[pack];
        }
        if (file < fileCounts_.size() && std::ranges::none_of(earlier, [&](const auto& row) { return row[N - 2] == file; })) {
            if (fileCounts_[file]++ == 0) {
                filePacks_[file] = pack;
            }
        }
    }
}

void PackFilter::Count(const std::span<const PackCatalogRepository::ExemplarRow> rows) {
    Count_(rows);
}

void PackFilter::Count(const std::span<const PackCatalogRepository::TextureRow> rows) {
    Count_(rows);
}

std::string PackFilter::Label_(const int32_t choice) const {
    char text[160];
    if (choice == kAllPacks) {
        std::snprintf(text, sizeof(text), "All packs (%zu)", total_);
    }
    else if (choice == kNoPack) {
        std::snprintf(text, sizeof(text), "Not in the catalog (%u)", counts_.back());
    }
    else if (choice <= kFirstFile) {
        const auto file = static_cast<uint32_t>(kFirstFile - choice);
        const std::string* name = catalog_->File(file);
        std::snprintf(text, sizeof(text), "%s (%u)", name ? name->c_str() : "?",
                      file < fileCounts_.size() ? fileCounts_[file] : 0);
    }
    else {
        std::snprintf(text, sizeof(text), "%s (%u)", catalog_->Packs()[static_cast<size_t>(choice)].name.c_str(),
                      counts_[static_cast<size_t>(choice)]);
    }
    return text;
}

void PackFilter::RefreshMatches_() {
    if (matchesValid_ && appliedSearch_ == search_) {
        return;
    }
    matchesValid_ = true;
    appliedSearch_ = search_;
    packMatches_.clear();
    fileMatches_.clear();
    const std::vector<TexturePack>& packs = catalog_->Packs();
    for (const int32_t pack : byName_) {
        if (ContainsAllWords(packs[static_cast<size_t>(pack)].name, appliedSearch_)) {
            packMatches_.push_back(pack);
        }
    }
    if (appliedSearch_.find_first_not_of(' ') == std::string::npos) {
        return;
    }
    for (const uint32_t file : filesByName_) {
        if (ContainsAllWords(*catalog_->File(file), appliedSearch_)) {
            fileMatches_.push_back(file);
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
        std::ranges::sort(byName_, {}, [&](const int32_t i) { return std::string_view(packs[static_cast<size_t>(i)].name); });
        for (uint32_t file = 0; file < fileCounts_.size(); ++file) {
            if (fileCounts_[file] > 0) {
                filesByName_.push_back(file);
            }
        }
        std::ranges::sort(filesByName_, {}, [&](const uint32_t file) { return std::string_view(*catalog_->File(file)); });
        sorted_ = true;
        matchesValid_ = false;
    }

    ImGui::SetNextItemWidth(-1);
    if (!ImGui::BeginCombo(id, Label_(selected_).c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Show only %s from one pack or file. The pack data comes from the SC4 Prop Texture Catalog.",
                              itemsName);
        }
        return false;
    }

    // The search box has the keyboard when the list opens. Enter takes the first match.
    if (ImGui::IsWindowAppearing()) {
        search_[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1.0f);
    const bool entered = ImGui::InputTextWithHint("##packSearch", "Type a pack or file name", search_, sizeof(search_),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    RefreshMatches_();
    const bool searching = appliedSearch_.find_first_not_of(' ') != std::string::npos;

    std::optional<int32_t> picked;
    const auto option = [&](const int32_t choice) {
        const bool isSelected = selected_ == choice;
        ImGui::PushID(choice);  // a pack and a file can have the same name
        if (ImGui::Selectable(Label_(choice).c_str(), isSelected)) {
            picked = choice;
        }
        ImGui::PopID();
        if (isSelected && ImGui::IsWindowAppearing()) {
            ImGui::SetScrollHereY();
        }
        if (!ImGui::IsItemHovered()) {
            return;
        }
        if (choice >= 0) {
            const TexturePack& info = packs[static_cast<size_t>(choice)];
            ImGui::SetTooltip("%s\n%s%s%s", info.author.c_str(), info.package.c_str(), info.package.empty() ? "" : "\n",
                              info.url.c_str());
        }
        else if (choice <= kFirstFile) {
            ImGui::SetTooltip("A file in %s", packs[filePacks_[static_cast<size_t>(kFirstFile - choice)]].name.c_str());
        }
    };

    // The list scrolls under the search box, which stays in view.
    if (ImGui::BeginChild("##packOptions", ImVec2(0.0f, ImGui::GetFontSize() * 18.0f))) {
        if (!searching) {
            option(kAllPacks);
        }
        for (const int32_t pack : packMatches_) {
            option(pack);
        }
        if (!searching && counts_.back() > 0) {
            option(kNoPack);
        }
        if (searching && packMatches_.empty() && fileMatches_.empty()) {
            ImGui::TextDisabled("Nothing matches.");
        }
        if (!fileMatches_.empty()) {
            ImGui::SeparatorText("Files");
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(fileMatches_.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    option(kFirstFile - static_cast<int32_t>(fileMatches_[static_cast<size_t>(i)]));
                }
            }
        }
    }
    ImGui::EndChild();

    if (entered && searching) {
        if (!packMatches_.empty()) {
            picked = packMatches_.front();
        }
        else if (!fileMatches_.empty()) {
            picked = kFirstFile - static_cast<int32_t>(fileMatches_.front());
        }
    }

    bool changed = false;
    if (picked) {
        // A selectable inside the scrolling list does not close the dropdown by itself.
        ImGui::CloseCurrentPopup();
        changed = selected_ != *picked;
        selected_ = *picked;
    }
    ImGui::EndCombo();
    return changed;
}

template <size_t N>
bool PackFilter::Passes_(const std::span<const std::array<uint32_t, N>> rows) const {
    if (selected_ == kAllPacks) {
        return true;
    }
    if (selected_ == kNoPack) {
        return rows.empty();
    }
    if (selected_ <= kFirstFile) {
        const auto file = static_cast<uint32_t>(kFirstFile - selected_);
        return std::ranges::any_of(rows, [&](const auto& row) { return row[N - 2] == file; });
    }
    return std::ranges::any_of(rows, [&](const auto& row) { return row[N - 1] == static_cast<uint32_t>(selected_); });
}

bool PackFilter::Passes(const std::span<const PackCatalogRepository::ExemplarRow> rows) const {
    return Passes_(rows);
}

bool PackFilter::Passes(const std::span<const PackCatalogRepository::TextureRow> rows) const {
    return Passes_(rows);
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
