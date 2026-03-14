#include <args.hxx>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <fstream>

#include "../shared/entities.hpp"
#include "../shared/index.hpp"
#include "DBPFReader.h"
#include "DbpfIndexService.hpp"
#include "ExemplarParser.hpp"
#include "BuiltinPropFamilyNames.hpp"
#include "PluginLocator.hpp"
#include "PropertyMapper.hpp"
#include "Utils.hpp"

#include <rfl/cbor.hpp>

#include "../dll/common/Utils.hpp"
#include "ThumbnailBinWriter.hpp"

#ifndef SC4_PLOP_AND_PAINT_VERSION
#define SC4_PLOP_AND_PAINT_VERSION "0.0.1"
#endif

namespace fs = std::filesystem;

namespace {
    constexpr uint32_t kTypeIdExemplar = 0x6534284Au;
    constexpr uint32_t kTypeIdCohort = 0x05342861u;
    constexpr uint32_t kMinThumbnailSize = kDefaultThumbnailSize / 2;
    constexpr uint32_t kMaxThumbnailSize = kDefaultThumbnailSize * 4;

    const char* GetFirstEnvironmentValue(std::initializer_list<const char*> names) {
        for (const char* name : names) {
            if (const char* value = std::getenv(name); value && value[0] != '\0') {
                return value;
            }
        }
        return nullptr;
    }

    struct RgbaImage {
        std::vector<std::byte> pixels;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    RgbaImage ResizeRgbaImage(const RgbaImage& source, const uint32_t targetWidth, const uint32_t targetHeight) {
        if (source.width == 0 || source.height == 0 || source.pixels.empty() || targetWidth == 0 || targetHeight == 0) {
            return {};
        }

        if (source.width == targetWidth && source.height == targetHeight) {
            return source;
        }

        RgbaImage resized;
        resized.width = targetWidth;
        resized.height = targetHeight;
        resized.pixels.resize(static_cast<size_t>(targetWidth) * targetHeight * 4);

        const float scaleX = static_cast<float>(source.width) / static_cast<float>(targetWidth);
        const float scaleY = static_cast<float>(source.height) / static_cast<float>(targetHeight);

        for (uint32_t y = 0; y < targetHeight; ++y) {
            const uint32_t srcY = std::min(static_cast<uint32_t>(y * scaleY), source.height - 1);
            for (uint32_t x = 0; x < targetWidth; ++x) {
                const uint32_t srcX = std::min(static_cast<uint32_t>(x * scaleX), source.width - 1);
                const size_t srcIndex = (static_cast<size_t>(srcY) * source.width + srcX) * 4;
                const size_t dstIndex = (static_cast<size_t>(y) * targetWidth + x) * 4;
                for (size_t c = 0; c < 4; ++c) {
                    resized.pixels[dstIndex + c] = source.pixels[srcIndex + c];
                }
            }
        }

        return resized;
    }

    Thumbnail NormalizeThumbnailToSquare(const Thumbnail& thumbnail, const uint32_t targetSize) {
        return rfl::visit(
            [targetSize](const auto& variant) -> Thumbnail {
                using Variant = std::decay_t<decltype(variant)>;

                RgbaImage source;
                source.width = variant.width;
                source.height = variant.height;
                source.pixels.resize(variant.data.size());
                std::memcpy(source.pixels.data(), variant.data.data(), variant.data.size());

                if (source.width == 0 || source.height == 0) {
                    Variant normalized;
                    normalized.width = targetSize;
                    normalized.height = targetSize;
                    normalized.data = rfl::Bytestring(std::vector<std::byte>(static_cast<size_t>(targetSize) * targetSize * 4));
                    return Thumbnail{std::move(normalized)};
                }

                RgbaImage content = source;
                if constexpr (std::is_same_v<Variant, Icon>) {
                    if (source.width != targetSize || source.height != targetSize) {
                        const float scale = std::min(
                            static_cast<float>(targetSize) / static_cast<float>(source.width),
                            static_cast<float>(targetSize) / static_cast<float>(source.height));
                        const uint32_t scaledWidth = std::max(1u, static_cast<uint32_t>(source.width * scale));
                        const uint32_t scaledHeight = std::max(1u, static_cast<uint32_t>(source.height * scale));
                        content = ResizeRgbaImage(source, scaledWidth, scaledHeight);
                    }
                }
                else if (source.width > targetSize || source.height > targetSize) {
                    const float scale = std::min(
                        static_cast<float>(targetSize) / static_cast<float>(source.width),
                        static_cast<float>(targetSize) / static_cast<float>(source.height));
                    const uint32_t scaledWidth = std::max(1u, static_cast<uint32_t>(source.width * scale));
                    const uint32_t scaledHeight = std::max(1u, static_cast<uint32_t>(source.height * scale));
                    content = ResizeRgbaImage(source, scaledWidth, scaledHeight);
                }

                std::vector<std::byte> squarePixels(static_cast<size_t>(targetSize) * targetSize * 4, std::byte{0});
                const uint32_t offsetX = (targetSize - content.width) / 2;
                const uint32_t offsetY = (targetSize - content.height) / 2;

                for (uint32_t y = 0; y < content.height; ++y) {
                    const size_t srcOffset = static_cast<size_t>(y) * content.width * 4;
                    const size_t dstOffset =
                        (static_cast<size_t>(y + offsetY) * targetSize + offsetX) * 4;
                    std::memcpy(squarePixels.data() + dstOffset, content.pixels.data() + srcOffset,
                                static_cast<size_t>(content.width) * 4);
                }

                Variant normalized;
                normalized.width = targetSize;
                normalized.height = targetSize;
                normalized.data = rfl::Bytestring(std::move(squarePixels));
                return Thumbnail{std::move(normalized)};
            },
            thumbnail);
    }

    void NormalizeThumbnailEntries(std::vector<std::pair<uint64_t, Thumbnail>>& entries, const uint32_t targetSize) {
        for (auto& [_, thumbnail] : entries) {
            thumbnail = NormalizeThumbnailToSquare(thumbnail, targetSize);
        }
    }

    PluginConfiguration GetDefaultPluginConfiguration() {
        PluginConfiguration config{};
        config.localeDir = "English";

        const char* userProfile = GetFirstEnvironmentValue({"USERPROFILE"});
        const char* programFiles = GetFirstEnvironmentValue({"PROGRAMFILES(X86)", "PROGRAMFILES(x86)", "PROGRAMFILES"});

        if (programFiles) {
            config.gameRoot = fs::path(programFiles) / "SimCity 4 Deluxe Edition";
            config.gamePluginsRoot = config.gameRoot / "Plugins";
        }

        if (userProfile) {
            config.userPluginsRoot = fs::path(userProfile) / "Documents" / "SimCity 4" / "Plugins";
        }

        return config;
    }

    bool IsDirectoryEmpty(const fs::path& directory) {
        if (directory.empty()) {
            return false;
        }

        std::error_code ec;
        if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) {
            return false;
        }

        const fs::directory_iterator begin(directory, fs::directory_options::skip_permission_denied, ec);
        if (ec) {
            return false;
        }

        return begin == fs::directory_iterator();
    }

    void ScanAndAnalyzeExemplars(const PluginConfiguration& config,
                                 spdlog::logger& logger,
                                 bool renderModelThumbnails,
                                 const uint32_t thumbnailSize) {
        try {
            logger.info("Initializing plugin scanner...");

            if (IsDirectoryEmpty(config.userPluginsRoot)) {
                logger.warn("User Plugins folder is empty: {}", config.userPluginsRoot.string());
            }

            // Create locator to discover plugin files
            PluginLocator locator(config);

            // Create and start the index service immediately for parallel indexing
            DbpfIndexService indexService(locator);
            logger.info("Starting background indexing service...");
            indexService.start();

            // While indexing happens in the background, load the property mapper
            logger.info("Loading property mapper...");
            PropertyMapper propertyMapper;
            auto mapperLoaded = false;

            // Try common locations for the property mapper XML
            std::vector<fs::path> mapperLocations{
                fs::path("PropertyMapper.xml"),
                fs::current_path() / "PropertyMapper.xml",
                config.gameRoot / "PropertyMapper.xml"
            };

            for (const auto& loc : mapperLocations) {
                if (fs::exists(loc)) {
                    if (propertyMapper.loadFromXml(loc)) {
                        logger.info("Loaded property mapper from: {}", loc.string());
                        mapperLoaded = true;
                        break;
                    }
                }
            }

            if (!mapperLoaded) {
                logger.warn("Could not load PropertyMapper XML - some features may be limited");
            }

            // Wait for indexing to complete, logging progress periodically
            logger.info("Waiting for indexing to complete...");
            using namespace std::chrono_literals;
            auto logIntervalCount = 0;
            while (true) {
                auto progress = indexService.snapshot();

                // Check if done
                if (progress.done) {
                    break;
                }

                std::this_thread::sleep_for(100ms);

                // Log progress every 2 seconds
                if (++logIntervalCount % 20 == 0) {
                    logger.info("  Indexing progress: {}/{} files processed, {} entries indexed",
                                progress.processedFiles, progress.totalFiles, progress.entriesIndexed);
                }
            }

            // Log final indexing results
            auto finalProgress = indexService.snapshot();
            logger.info("Indexing complete: {} files processed, {} entries indexed, {} errors",
                        finalProgress.processedFiles, finalProgress.totalFiles, finalProgress.errorCount);

            uint32_t buildingsFound = 0;
            uint32_t lotsFound = 0;
            uint32_t parseErrors = 0;
            std::set<uint32_t> missingBuildingIds;

            ExemplarParser parser(propertyMapper, &indexService, renderModelThumbnails, thumbnailSize);
            std::vector<Building> allBuildings;
            std::vector<Prop> allProps;
            std::vector<Flora> allFlora;
            std::unordered_map<uint32_t, std::string> propFamilyNamesById;
            std::unordered_map<uint32_t, ParsedBuildingExemplar> buildingMap;
            std::unordered_map<uint32_t, Building> builtBuildings;
            std::unordered_set<uint64_t> seenLotKeys;
            std::unordered_set<uint64_t> seenPropKeys;
            std::unordered_set<uint64_t> seenFloraKeys;

            // Use the index service to get exemplars and cohorts across all files.
            logger.info("Processing exemplar/cohort records using type index...");

            // Group record TGIs by file for efficient batch processing.
            std::unordered_map<fs::path, std::vector<DBPF::Tgi>> fileToExemplarTgis;
            {
                auto exemplarTgis = indexService.typeIndex(kTypeIdExemplar);
                auto cohortTgis = indexService.typeIndex(kTypeIdCohort);

                logger.info("Found {} exemplars and {} cohorts to process",
                            exemplarTgis.size(), cohortTgis.size());

                std::vector<DBPF::Tgi> recordTgis;
                recordTgis.reserve(exemplarTgis.size() + cohortTgis.size());
                recordTgis.insert(recordTgis.end(), exemplarTgis.begin(), exemplarTgis.end());
                recordTgis.insert(recordTgis.end(), cohortTgis.begin(), cohortTgis.end());

                for (const auto& tgi : recordTgis) {
                    auto files = indexService.lookupFiles(tgi);
                    if (!files.empty()) {
                        fileToExemplarTgis[files[0]].push_back(tgi);
                    }
                }
            }

            size_t filesProcessed = 0;

            // Store lot config TGIs for second pass
            std::vector<std::pair<fs::path, DBPF::Tgi>> lotConfigTgis;

            for (const auto& [filePath, tgis] : fileToExemplarTgis) {
                try {
                    // Get cached reader from index service
                    auto* reader = indexService.getReader(filePath);
                    if (!reader) {
                        logger.warn("Failed to get reader for file: {}", filePath.string());
                        continue;
                    }

                    logger.debug("Processing {} exemplars from {}", tgis.size(), filePath.filename().string());

                    // Process all exemplars in this file
                    for (const auto& tgi : tgis) {
                        try {
                            auto exemplarResult = reader->LoadExemplar(tgi);
                            if (!exemplarResult.has_value()) {
                                continue;
                            }

                            if (tgi.type == kTypeIdCohort) {
                                if (auto parsedFamily = parser.parsePropFamilyFromCohort(*exemplarResult)) {
                                    const uint32_t familyId = parsedFamily->familyId.value();
                                    auto [it, inserted] = propFamilyNamesById.emplace(
                                        familyId, parsedFamily->displayName);
                                    if (!inserted && it->second != parsedFamily->displayName) {
                                        spdlog::debug(
                                            "Duplicate prop family name for 0x{:08X}: keeping '{}', ignoring '{}'",
                                            familyId, it->second, parsedFamily->displayName);
                                    }
                                }
                                continue;
                            }

                            auto exemplarType = parser.getExemplarType(*exemplarResult);
                            if (!exemplarType) {
                                continue;
                            }

                            if (tgi.type != kTypeIdExemplar) {
                                continue;
                            }

                            if (*exemplarType == ExemplarType::Building) {
                                auto building = parser.parseBuilding(*exemplarResult, tgi);
                                if (building) {
                                    buildingMap[tgi.instance] = *building;
                                    builtBuildings.try_emplace(tgi.instance, parser.buildingFromParsed(*building));
                                    buildingsFound++;
                                    logger.trace("  Building: {} (0x{:08X})", building->name, tgi.instance);
                                }
                            }
                            else if (*exemplarType == ExemplarType::LotConfig) {
                                // Queue for second pass
                                lotConfigTgis.emplace_back(filePath, tgi);
                            }
                            else if (*exemplarType == ExemplarType::Prop) {
                                if (seenPropKeys.contains(MakeGIKey(tgi.group, tgi.instance))) {
                                    logger.warn("Duplicate prop skipped: (group=0x{:08X}, instance=0x{:08X})",
                                                tgi.group, tgi.instance);
                                    continue;
                                }
                                if (auto prop = parser.parseProp(*exemplarResult, tgi)) {
                                    logger.trace("  Prop: {} (0x{:08X})", prop->visibleName, tgi.instance);
                                    allProps.emplace_back(
                                        parser.propFromParsed(*prop)
                                    );
                                    seenPropKeys.insert(MakeGIKey(tgi.group, tgi.instance));
                                }
                            }
                            else if (*exemplarType == ExemplarType::Flora) {
                                if (seenFloraKeys.contains(MakeGIKey(tgi.group, tgi.instance))) {
                                    logger.warn("Duplicate flora skipped: (group=0x{:08X}, instance=0x{:08X})",
                                                tgi.group, tgi.instance);
                                    continue;
                                }
                                if (auto flora = parser.parseFlora(*exemplarResult, tgi)) {
                                    logger.trace("  Flora: {} (0x{:08X})", flora->visibleName, tgi.instance);
                                    allFlora.emplace_back(parser.floraFromParsed(*flora));
                                    seenFloraKeys.insert(MakeGIKey(tgi.group, tgi.instance));
                                }
                            }
                        }
                        catch (const std::exception& error) {
                            logger.debug("Error processing TGI {}/{}/{}: {}",
                                         tgi.type, tgi.group, tgi.instance, error.what());
                            parseErrors++;
                        }
                    }

                    filesProcessed++;

                    // Log progress periodically
                    if (filesProcessed % 100 == 0) {
                        logger.info("  Processed {}/{} files ({} buildings found so far)",
                                    filesProcessed, fileToExemplarTgis.size(), buildingsFound);
                    }
                }
                catch (const std::exception& error) {
                    logger.warn("Error processing file {}: {}", filePath.filename().string(), error.what());
                }
            }

            fileToExemplarTgis.clear();
            seenPropKeys.clear();

            // Build family-to-buildings map for resolving growable lot references
            std::unordered_map<uint32_t, std::vector<uint32_t>> familyToBuildingsMap;
            for (const auto& [instanceId, building] : buildingMap) {
                for (uint32_t familyId : building.familyIds) {
                    familyToBuildingsMap[familyId].push_back(instanceId);
                }
            }

            for (const auto& [filePath, tgi] : lotConfigTgis) {
                try {
                    auto* reader = indexService.getReader(filePath);
                    if (!reader) {
                        continue;
                    }

                    auto exemplarResult = reader->LoadExemplar(tgi);
                    if (!exemplarResult.has_value()) {
                        continue;
                    }

                    if (auto parsedLot = parser.
                        parseLotConfig(*exemplarResult, tgi, buildingMap, familyToBuildingsMap)) {
                        // Get building for this lot
                        auto buildingIt = builtBuildings.find(parsedLot->buildingInstanceId);
                        if (buildingIt != builtBuildings.end()) {
                            Lot lot = parser.lotFromParsed(*parsedLot);
                            if (parsedLot->isFamilyReference) {
                                logger.trace("  Lot: {} (0x{:08X}) [family 0x{:08X} -> building 0x{:08X}]",
                                             lot.name, lot.instanceId.get(),
                                             parsedLot->buildingFamilyId, parsedLot->buildingInstanceId);
                            }
                            else {
                                logger.trace("  Lot: {} (0x{:08X})", lot.name, lot.instanceId.get());
                            }
                            const uint64_t lotKey = (static_cast<uint64_t>(lot.groupId.value()) << 32) |
                                static_cast<uint64_t>(lot.instanceId.value());
                            if (seenLotKeys.insert(lotKey).second) {
                                buildingIt->second.lots.push_back(lot);
                                lotsFound++;
                            }
                            else {
                                logger.warn("Duplicate lot skipped: {} (group=0x{:08X}, instance=0x{:08X})",
                                            lot.name, lot.groupId.value(), lot.instanceId.value());
                            }
                        }
                        else {
                            if (parsedLot->isFamilyReference) {
                                logger.warn(
                                    "  Lot {} references family 0x{:08X} but resolved building 0x{:08X} not found",
                                    parsedLot->name, parsedLot->buildingFamilyId, parsedLot->buildingInstanceId);
                            }
                            else {
                                logger.warn("  Lot {} references unknown building 0x{:08X}",
                                            parsedLot->name, parsedLot->buildingInstanceId);
                            }
                            missingBuildingIds.insert(parsedLot->buildingInstanceId);
                        }
                    }
                }
                catch (const std::exception& error) {
                    logger.debug("Error processing lot config TGI {}/{}/{}: {}",
                                 tgi.type, tgi.group, tgi.instance, error.what());
                    parseErrors++;
                }
            }

            buildingMap.clear();

            if (!missingBuildingIds.empty()) {
                logger.warn("Missing building references for {} lots:", missingBuildingIds.size());
            }

            // Collect buildings that actually have lots
            for (auto& [instanceId, building] : builtBuildings) {
                if (!building.lots.empty()) {
                    allBuildings.push_back(std::move(building));
                }
            }

            builtBuildings.clear();

            logger.info("Scan complete: {} buildings with lots, {} lots, {} parse errors",
                        allBuildings.size(), lotsFound, parseErrors);

            SanitizeStrings(allBuildings, allProps);

            for (const auto& prop : allProps) {
                for (const auto& familyIdHex : prop.familyIds) {
                    const uint32_t familyId = familyIdHex.value();
                    if (familyId == 0) {
                        continue;
                    }

                    if (propFamilyNamesById.contains(familyId)) {
                        continue;
                    }

                    std::string displayName;
                    if (const auto it = kBuiltinPropFamilyNames.find(familyId); it != kBuiltinPropFamilyNames.end()) {
                        displayName = std::string(it->second);
                    }
                    else {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "Family 0x%08X", familyId);
                        displayName = buf;
                    }

                    propFamilyNamesById.emplace(familyId, std::move(displayName));
                }
            }

            std::vector<PropFamilyInfo> propFamilies;
            propFamilies.reserve(propFamilyNamesById.size());
            for (auto& [familyId, displayName] : propFamilyNamesById) {
                propFamilies.push_back(PropFamilyInfo{
                    rfl::Hex<uint32_t>(familyId),
                    std::move(displayName)
                });
            }
            std::sort(propFamilies.begin(), propFamilies.end(), [](const PropFamilyInfo& a, const PropFamilyInfo& b) {
                return a.familyId.value() < b.familyId.value();
            });

            // Extract building thumbnails into a sidecar binary file, then strip them
            // from allBuildings so the CBOR stays lean.
            {
                std::vector<std::pair<uint64_t, Thumbnail>> buildingThumbnails;
                for (auto& b : allBuildings) {
                    if (b.thumbnail.has_value()) {
                        const uint64_t key = MakeGIKey(b.groupId.value(), b.instanceId.value());
                        buildingThumbnails.emplace_back(key, std::move(*b.thumbnail));
                        b.thumbnail.reset();
                    }
                }
                if (!buildingThumbnails.empty()) {
                    NormalizeThumbnailEntries(buildingThumbnails, thumbnailSize);
                    const auto binPath = config.userPluginsRoot / "lot_thumbnails.bin";
                    const auto count = buildingThumbnails.size();
                    ThumbnailBin::Write(binPath, std::move(buildingThumbnails));
                    logger.info("Exported {} building thumbnails to {}", count, binPath.string());
                }
            }

            // Export grouped building/lot data to CBOR file in user plugins directory
            if (!allBuildings.empty()) {
                try {
                    auto cborPath = config.userPluginsRoot / "lots.cbor";
                    fs::create_directories(config.userPluginsRoot);

                    logger.info("Exporting {} buildings ({} lots) to {}", allBuildings.size(), lotsFound,
                                cborPath.string());

                    if (std::ofstream file(cborPath, std::ios::binary); !file) {
                        logger.error("Failed to open file for writing: {}", cborPath.string());
                    }
                    else {
                        rfl::cbor::write(allBuildings, file);
                        file.close();
                        logger.info("Successfully exported lot configs");
                    }
                }
                catch (const std::exception& error) {
                    logger.error("Error exporting lot configs: {}", error.what());
                }
            }

            // Extract prop thumbnails into a sidecar binary file, then strip them.
            {
                std::vector<std::pair<uint64_t, Thumbnail>> propThumbnails;
                for (auto& p : allProps) {
                    if (p.thumbnail.has_value()) {
                        const uint64_t key = MakeGIKey(p.groupId.value(), p.instanceId.value());
                        propThumbnails.emplace_back(key, std::move(*p.thumbnail));
                        p.thumbnail.reset();
                    }
                }
                if (!propThumbnails.empty()) {
                    NormalizeThumbnailEntries(propThumbnails, thumbnailSize);
                    const auto binPath = config.userPluginsRoot / "prop_thumbnails.bin";
                    const auto count = propThumbnails.size();
                    ThumbnailBin::Write(binPath, std::move(propThumbnails));
                    logger.info("Exported {} prop thumbnails to {}", count, binPath.string());
                }
            }

            if (!allProps.empty() || !propFamilies.empty()) {
                try {
                    auto cborPath = config.userPluginsRoot / "props.cbor";
                    fs::create_directories(config.userPluginsRoot);

                    PropsCache propsCache;
                    propsCache.props = std::move(allProps);
                    propsCache.propFamilies = std::move(propFamilies);

                    logger.info("Exporting {} props and {} prop families to {}",
                                propsCache.props.size(), propsCache.propFamilies.size(), cborPath.string());

                    if (std::ofstream file(cborPath, std::ios::binary); !file) {
                        logger.error("Failed to open file for writing: {}", cborPath.string());
                    }
                    else {
                        rfl::cbor::write(propsCache, file);
                        file.close();
                        logger.info("Successfully exported props");
                    }
                }
                catch (const std::exception& error) {
                    logger.error("Error exporting props: {}", error.what());
                }
            }

            // Extract flora thumbnails into a sidecar binary file, then strip them.
            {
                std::vector<std::pair<uint64_t, Thumbnail>> floraThumbnails;
                for (auto& f : allFlora) {
                    if (f.thumbnail.has_value()) {
                        const uint64_t key = MakeGIKey(f.groupId.value(), f.instanceId.value());
                        floraThumbnails.emplace_back(key, std::move(*f.thumbnail));
                        f.thumbnail.reset();
                    }
                }
                if (!floraThumbnails.empty()) {
                    NormalizeThumbnailEntries(floraThumbnails, thumbnailSize);
                    const auto binPath = config.userPluginsRoot / "flora_thumbnails.bin";
                    const auto count = floraThumbnails.size();
                    ThumbnailBin::Write(binPath, std::move(floraThumbnails));
                    logger.info("Exported {} flora thumbnails to {}", count, binPath.string());
                }
            }

            if (!allFlora.empty()) {
                try {
                    auto cborPath = config.userPluginsRoot / "flora.cbor";
                    fs::create_directories(config.userPluginsRoot);

                    FloraCache floraCache;
                    floraCache.floraItems = std::move(allFlora);

                    logger.info("Exporting {} flora items to {}", floraCache.floraItems.size(), cborPath.string());

                    if (std::ofstream file(cborPath, std::ios::binary); !file) {
                        logger.error("Failed to open file for writing: {}", cborPath.string());
                    }
                    else {
                        rfl::cbor::write(floraCache, file);
                        file.close();
                        logger.info("Successfully exported flora");
                    }
                }
                catch (const std::exception& error) {
                    logger.error("Error exporting flora: {}", error.what());
                }
            }

            // Shutdown the indexing service
            indexService.shutdown();
        }
        catch (const std::exception& error) {
            logger.error("Error during exemplar scan: {}", error.what());
        }
    }
} // namespace

int main(int argc, char* argv[]) {
    try {
        auto logger = spdlog::stdout_color_mt("lotplop-cli");
        spdlog::set_default_logger(logger);
        logger->set_level(spdlog::level::debug);
        spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
        logger->info("SC4PlopAndPaint CLI {}", SC4_PLOP_AND_PAINT_VERSION);

        args::ArgumentParser parser("SC4PlopAndPaint CLI",
                                    "Inspect and extract Lot and Building exemplars from SimCity 4 plugins.");
        args::HelpFlag helpFlag(parser, "help", "Show this help message", {'h', "help"});
        args::Flag versionFlag(parser, "version", "Print version", {"version"});
        args::Flag scanFlag(parser, "scan", "Scan plugins and extract exemplars", {"scan"});
        args::ValueFlag<std::string> gameFlag(parser, "path", "Game root directory (plugins will be in {path}/Plugins)",
                                              {"game"});
        args::ValueFlag<std::string> pluginsFlag(parser, "path", "User plugins directory", {"plugins"});
        args::ValueFlag<std::string> localeFlag(parser, "path", "Locale directory under game root (e.g., English)",
                                                {"locale"});
        args::Flag renderThumbnailsFlag(parser, "render-thumbnails", "Render 3D thumbnails for buildings without icons",
                                        {"render-thumbnails"});
        args::ValueFlag<uint32_t> thumbnailSizeFlag(
            parser,
            "px",
            "Square thumbnail size in pixels for cached thumbnails (22-176, default 44)",
            {"thumbnail-size"});

        try {
            parser.ParseCLI(argc, argv);
        }
        catch (const args::Completion& e) {
            std::cout << e.what();
        }
        catch (const args::Help&) {
            std::cout << parser.Help() << std::endl;
            return 0;
        }
        catch (const args::ParseError& error) {
            std::cerr << error.what() << std::endl;
            std::cerr << parser.Help() << std::endl;
            return 1;
        }

        if (versionFlag) {
            logger->info("Version: {}", SC4_PLOP_AND_PAINT_VERSION);
            return 0;
        }

        if (scanFlag) {
            auto config = GetDefaultPluginConfiguration();
            uint32_t thumbnailSize = kDefaultThumbnailSize;

            // Override with command-line arguments if provided
            if (gameFlag) {
                config.gameRoot = args::get(gameFlag);
                config.gamePluginsRoot = config.gameRoot / "Plugins";
            }
            if (localeFlag) {
                config.localeDir = args::get(localeFlag);
            }
            if (pluginsFlag) {
                config.userPluginsRoot = args::get(pluginsFlag);
            }
            if (thumbnailSizeFlag) {
                thumbnailSize = args::get(thumbnailSizeFlag);
                if (thumbnailSize < kMinThumbnailSize || thumbnailSize > kMaxThumbnailSize) {
                    logger->error("Invalid --thumbnail-size {}. Expected a value between {} and {}.",
                                  thumbnailSize, kMinThumbnailSize, kMaxThumbnailSize);
                    return 1;
                }
            }

            logger->info("Using plugin configuration:");
            logger->info("  Game Root: {}", config.gameRoot.string());
            logger->info("  Game Locale: {}", (config.gameRoot / config.localeDir).string());
            logger->info("  Game Plugins: {}", config.gamePluginsRoot.string());
            logger->info("  User Plugins: {}", config.userPluginsRoot.string());
            logger->info("  Thumbnail Size: {} px", thumbnailSize);

            if (renderThumbnailsFlag) {
                logger->info("3D thumbnail rendering enabled");
            }
            ScanAndAnalyzeExemplars(config, *logger, renderThumbnailsFlag, thumbnailSize);
            return 0;
        }

        // Default behavior - show plugin paths
        auto config = GetDefaultPluginConfiguration();

        // Override with command-line arguments if provided
        if (gameFlag) {
            config.gameRoot = args::get(gameFlag);
            config.gamePluginsRoot = config.gameRoot / "Plugins";
        }
        if (localeFlag) {
            config.localeDir = args::get(localeFlag);
        }
        if (pluginsFlag) {
            config.userPluginsRoot = args::get(pluginsFlag);
        }

        logger->info("Plugin directories:");
        logger->info("  Game Root: {}", config.gameRoot.string());
        logger->info("  Game Locale: {}", (config.gameRoot / config.localeDir).string());
        logger->info("  Game Plugins: {}", config.gamePluginsRoot.string());
        logger->info("  User Plugins: {}", config.userPluginsRoot.string());
        logger->info("Use --scan to scan and extract exemplars");

        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << std::endl;
        return 1;
    }
}
