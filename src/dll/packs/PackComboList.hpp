#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../common/Utils.hpp"
#include "PackCatalogRepository.hpp"
#include "imgui.h"

// The inside of a pack dropdown, between BeginCombo and EndCombo: a search box that has the
// keyboard when the list opens, and the packs whose name contains what is typed. Enter takes the
// first pack that matches. label(pack) gives the text of an entry. True when selected changed.
template <typename Label>
bool PackComboList(char (&search)[64],
                   const std::vector<TexturePack>& packs,
                   const std::vector<int32_t>& packsByName,
                   const bool hasUnknown,
                   const int32_t allPacks,
                   const int32_t noPack,
                   int32_t& selected,
                   Label&& label) {
    if (ImGui::IsWindowAppearing()) {
        search[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1.0f);
    const bool entered = ImGui::InputTextWithHint("##packSearch", "Type to find a pack", search, sizeof(search),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    const bool searching = search[0] != '\0';

    std::optional<int32_t> picked;
    std::optional<int32_t> firstMatch;
    const auto option = [&](const int32_t pack) {
        const bool isSelected = selected == pack;
        if (ImGui::Selectable(label(pack).c_str(), isSelected)) {
            picked = pack;
        }
        if (isSelected && ImGui::IsWindowAppearing()) {
            ImGui::SetScrollHereY();
        }
        if (pack >= 0 && ImGui::IsItemHovered()) {
            const TexturePack& info = packs[static_cast<size_t>(pack)];
            ImGui::SetTooltip("%s\n%s%s%s", info.author.c_str(), info.package.c_str(), info.package.empty() ? "" : "\n",
                              info.url.c_str());
        }
    };

    // The list scrolls under the search box, which stays in view.
    if (ImGui::BeginChild("##packOptions", ImVec2(0.0f, ImGui::GetFontSize() * 18.0f))) {
        if (!searching) {
            option(allPacks);
        }
        for (const int32_t pack : packsByName) {
            if (ContainsCaseInsensitive(packs[static_cast<size_t>(pack)].name, search)) {
                if (!firstMatch) {
                    firstMatch = pack;
                }
                option(pack);
            }
        }
        if (searching && !firstMatch) {
            ImGui::TextDisabled("No packs match.");
        }
        if (!searching && hasUnknown) {
            option(noPack);
        }
    }
    ImGui::EndChild();

    if (entered && searching && firstMatch) {
        picked = firstMatch;
    }
    if (!picked) {
        return false;
    }
    // A selectable inside the scrolling list does not close the dropdown by itself.
    ImGui::CloseCurrentPopup();
    const bool changed = selected != *picked;
    selected = *picked;
    return changed;
}
