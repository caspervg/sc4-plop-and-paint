#include "PackCatalogRepository.hpp"

#include <algorithm>
#include <filesystem>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include "../utils/Logger.h"
#include "rfl/cbor/load.hpp"

void PackCatalogRepository::Load() {
    catalog_ = {};
    try {
        const auto path = std::filesystem::path(wil::GetModuleFileNameW(wil::GetModuleInstanceHandle()).get())
                              .parent_path() / "packs.cbor";
        if (!std::filesystem::exists(path)) {
            LOG_INFO("PackCatalogRepository: no {}, textures and props show no pack", path.string());
            return;
        }
        auto result = rfl::cbor::load<PackCatalog, rfl::DefaultIfMissing>(path.string());
        if (!result) {
            LOG_WARN("PackCatalogRepository: could not read {}: {}", path.string(), result.error().what());
            return;
        }
        catalog_ = std::move(*result);
    }
    catch (const std::exception& e) {
        LOG_WARN("PackCatalogRepository: could not load the pack catalog: {}", e.what());
        return;
    }

    // Drop rows that point outside the lists, so lookups need no checks, and sort them in case an
    // older cache builder wrote them in another order.
    const auto outside = [this](const uint32_t file, const uint32_t pack) {
        return file >= catalog_.files.size() || pack >= catalog_.packs.size();
    };
    std::erase_if(catalog_.textures, [&](const TextureRow& row) { return outside(row[1], row[2]); });
    std::erase_if(catalog_.props, [&](const PropRow& row) { return outside(row[2], row[3]); });
    std::ranges::sort(catalog_.textures);
    std::ranges::sort(catalog_.props);

    LOG_INFO("PackCatalogRepository: {} texture and {} prop rows in {} packs, catalog updated {}, fetched {}",
             catalog_.textures.size(), catalog_.props.size(), catalog_.packs.size(), catalog_.catalogUpdatedAt,
             catalog_.fetchedAt);
}

std::span<const PackCatalogRepository::TextureRow> PackCatalogRepository::TextureRows(const uint32_t instanceId) const {
    const auto [first, last] = std::ranges::equal_range(catalog_.textures, instanceId, {},
                                                        [](const TextureRow& row) { return row[0]; });
    return {first, last};
}

std::span<const PackCatalogRepository::PropRow> PackCatalogRepository::PropRows(const uint32_t groupId,
                                                                                const uint32_t instanceId) const {
    const auto [first, last] = std::ranges::equal_range(catalog_.props, std::pair{groupId, instanceId}, {},
                                                        [](const PropRow& row) { return std::pair{row[0], row[1]}; });
    return {first, last};
}

const TexturePack* PackCatalogRepository::Pack(const int32_t index) const {
    return index >= 0 && static_cast<size_t>(index) < catalog_.packs.size() ? &catalog_.packs[static_cast<size_t>(index)]
                                                                             : nullptr;
}

const std::string* PackCatalogRepository::File(const uint32_t index) const {
    return index < catalog_.files.size() ? &catalog_.files[index] : nullptr;
}
