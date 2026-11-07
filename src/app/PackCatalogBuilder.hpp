#pragma once

#include <filesystem>
#include <optional>

#include <spdlog/logger.h>

#include "packs.hpp"

// Fetches the latest SC4 Prop Texture Catalog database and turns it into the PackCatalog the DLL
// reads from packs.cbor.
namespace PackCatalogBuilder {
    // Downloads the catalog to workDir (removed again afterwards) and reads it. With the current
    // catalog, the download is skipped when it has not changed since; that one comes back with a
    // new fetchedAt.
    [[nodiscard]] std::optional<PackCatalog> FetchLatest(const std::filesystem::path& workDir, const PackCatalog* current,
                                                         spdlog::logger& logger);

    // Reads a catalog database already on disk; fetchedAt is left for the caller.
    [[nodiscard]] std::optional<PackCatalog> Read(const std::filesystem::path& databasePath, spdlog::logger& logger);
}
