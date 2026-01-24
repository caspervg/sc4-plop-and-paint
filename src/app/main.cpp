#include <args.hxx>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>
#include <set>
#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <fstream>

#include "DBPFReader.h"
#include "ExemplarParser.hpp"
#include "PropertyMapper.hpp"
#include "services/PluginLocator.hpp"
#include "services/DbpfIndexService.hpp"
#include "../shared/index.hpp"
#include "../shared/entities.hpp"

#include <rfl/cbor.hpp>

#ifndef SC4_ADVANCED_LOT_PLOP_VERSION
#define SC4_ADVANCED_LOT_PLOP_VERSION "0.0.0"
#endif

namespace fs = std::filesystem;

namespace {

PluginConfiguration GetDefaultPluginConfiguration()
{
    const char* userProfile = std::getenv("USERPROFILE");
    const char* programFiles = std::getenv("PROGRAMFILES(x86)");
    const auto gameRoot = fs::path(programFiles) / "SimCity 4 Deluxe Edition";
    if (userProfile && programFiles) {
        return PluginConfiguration{
            .gameRoot = gameRoot,
            .localeDir = "English",
            .gamePluginsRoot = gameRoot / "Plugins",
            .userPluginsRoot = fs::path(userProfile) / "Documents" / "SimCity 4" / "Plugins"
        };
    }
    return PluginConfiguration{};
}

void ScanAndAnalyzeExemplars(const PluginConfiguration& config,
                             spdlog::logger& logger,
                             bool renderModelThumbnails)
{
    try {
        logger.info("Initializing plugin scanner...");

        // Create locator to discover plugin files
        PluginLocator locator(config);

        // Create and start the index service immediately for parallel indexing
        DbpfIndexService indexService(locator);
        logger.info("Starting background indexing service...");
        indexService.start();

        // While indexing happens in background, load the property mapper
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
        int logIntervalCount = 0;
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

        ExemplarParser parser(propertyMapper, &indexService, renderModelThumbnails);
        std::vector<Lot> allLots;
        std::unordered_map<uint32_t, ParsedBuildingExemplar> buildingMap;

        // Use the index service to get all exemplars across all files
        logger.info("Processing exemplars using type index...");
        const auto& tgiIndex = indexService.tgiIndex();
        const auto exemplarTgis = indexService.typeIndex(0x6534284Au);

        logger.info("Found {} exemplars to process", exemplarTgis.size());

        // Group exemplar TGIs by file for efficient batch processing
        std::unordered_map<fs::path, std::vector<DBPF::Tgi>> fileToExemplarTgis;
        for (const auto& tgi : exemplarTgis) {
            const auto& filePaths = tgiIndex.at(tgi);
            // Add this TGI to the first file that contains it
            if (!filePaths.empty()) {
                fileToExemplarTgis[filePaths[0]].push_back(tgi);
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

                        auto exemplarType = parser.getExemplarType(*exemplarResult);
                        if (!exemplarType) {
                            continue;
                        }

                        if (*exemplarType == ExemplarType::Building) {
                            auto building = parser.parseBuilding(*exemplarResult, tgi);
                            if (building) {
                                buildingMap[tgi.instance] = *building;
                                buildingsFound++;
                                logger.trace("  Building: {} (0x{:08X})", building->name, tgi.instance);
                            }
                        } else if (*exemplarType == ExemplarType::LotConfig) {
                            // Queue for second pass
                            lotConfigTgis.emplace_back(filePath, tgi);
                        }

                    } catch (const std::exception& error) {
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

            } catch (const std::exception& error) {
                logger.warn("Error processing file {}: {}", filePath.filename().string(), error.what());
            }
        }

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

                auto parsedLot = parser.parseLotConfig(*exemplarResult, tgi, buildingMap, familyToBuildingsMap);
                if (parsedLot) {
                    // Get building for this lot
                    auto buildingIt = buildingMap.find(parsedLot->buildingInstanceId);
                    if (buildingIt != buildingMap.end()) {
                        Building building = parser.buildingFromParsed(buildingIt->second);
                        Lot lot = parser.lotFromParsed(*parsedLot, building);
                        if (parsedLot->isFamilyReference) {
                            logger.trace("  Lot: {} (0x{:08X}) [family 0x{:08X} -> building 0x{:08X}]",
                                       lot.name, lot.instanceId.get(),
                                       parsedLot->buildingFamilyId, parsedLot->buildingInstanceId);
                        } else {
                            logger.trace("  Lot: {} (0x{:08X})", lot.name, lot.instanceId.get());
                        }
                        allLots.push_back(lot);
                        lotsFound++;
                    } else {
                        if (parsedLot->isFamilyReference) {
                            logger.warn("  Lot {} references family 0x{:08X} but resolved building 0x{:08X} not found",
                                       parsedLot->name, parsedLot->buildingFamilyId, parsedLot->buildingInstanceId);
                        } else {
                            logger.warn("  Lot {} references unknown building 0x{:08X}",
                                       parsedLot->name, parsedLot->buildingInstanceId);
                        }
                        missingBuildingIds.insert(parsedLot->buildingInstanceId);
                    }
                }
            } catch (const std::exception& error) {
                logger.debug("Error processing lot config TGI {}/{}/{}: {}",
                           tgi.type, tgi.group, tgi.instance, error.what());
                parseErrors++;
            }
        }

        if (!missingBuildingIds.empty()) {
            logger.warn("Missing building references for {} lots:", missingBuildingIds.size());
        }

        logger.info("Scan complete: {} buildings, {} lots, {} parse errors",
                   buildingsFound, lotsFound, parseErrors);

        // Export lot config data to CBOR file in user plugins directory
        if (!allLots.empty()) {
            try {
                // Deduplicate lots by (group, instance) - keep the last occurrence
                struct PairHash {
                    size_t operator()(const std::pair<uint32_t, uint32_t>& p) const {
                        return std::hash<uint64_t>{}((static_cast<uint64_t>(p.first) << 32) | p.second);
                    }
                };
                std::unordered_map<std::pair<uint32_t, uint32_t>, Lot, PairHash> uniqueLots;
                for (auto& lot : allLots) {
                    auto key = std::make_pair(lot.groupId.value(), lot.instanceId.value());
                    uniqueLots[key] = std::move(lot);
                }

                // Convert back to vector
                std::vector<Lot> deduplicatedLots;
                deduplicatedLots.reserve(uniqueLots.size());
                for (auto& [key, lot] : uniqueLots) {
                    deduplicatedLots.push_back(std::move(lot));
                }

                const size_t duplicatesRemoved = allLots.size() - deduplicatedLots.size();
                if (duplicatesRemoved > 0) {
                    logger.warn("Removed {} duplicate lot(s) before export", duplicatesRemoved);
                }

                auto cborPath = config.userPluginsRoot / "lot_configs.cbor";
                fs::create_directories(config.userPluginsRoot);

                logger.info("Exporting {} unique lot configs to {}", deduplicatedLots.size(), cborPath.string());

                std::ofstream file(cborPath, std::ios::binary);
                if (!file) {
                    logger.error("Failed to open file for writing: {}", cborPath.string());
                } else {
                    rfl::cbor::write(deduplicatedLots, file);
                    file.close();
                    logger.info("Successfully exported lot configs");
                }
            } catch (const std::exception& error) {
                logger.error("Error exporting lot configs: {}", error.what());
            }
        }

        // Shutdown the indexing service
        indexService.shutdown();
    } catch (const std::exception& error) {
        logger.error("Error during exemplar scan: {}", error.what());
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        auto logger = spdlog::stdout_color_mt("lotplop-cli");
        logger->set_level(spdlog::level::debug);
        spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
        logger->info("SC4AdvancedLotPlop CLI {}", SC4_ADVANCED_LOT_PLOP_VERSION);

        args::ArgumentParser parser("SC4AdvancedLotPlop CLI", "Inspect and extract Lot and Building exemplars from SimCity 4 plugins.");
        args::HelpFlag helpFlag(parser, "help", "Show this help message", {'h', "help"});
        args::Flag versionFlag(parser, "version", "Print version", {"version"});
        args::Flag scanFlag(parser, "scan", "Scan plugins and extract exemplars", {"scan"});
        args::ValueFlag<std::string> gameFlag(parser, "path", "Game root directory (plugins will be in {path}/Plugins)", {"game"});
        args::ValueFlag<std::string> pluginsFlag(parser, "path", "User plugins directory", {"plugins"});
        args::ValueFlag<std::string> localeFlag(parser, "path", "Locale directory under game root (e.g., English)", {"locale"});
        args::Flag renderThumbnailsFlag(parser, "render-thumbnails", "Render 3D thumbnails for buildings without icons", {"render-thumbnails"});

        try {
            parser.ParseCLI(argc, argv);
        } catch (const args::Completion& e) {
            std::cout << e.what();
        } catch (const args::Help&) {
            std::cout << parser.Help() << std::endl;
            return 0;
        } catch (const args::ParseError& error) {
            std::cerr << error.what() << std::endl;
            std::cerr << parser.Help() << std::endl;
            return 1;
        }

        if (versionFlag) {
            logger->info("Version: {}", SC4_ADVANCED_LOT_PLOP_VERSION);
            return 0;
        }

        if (scanFlag) {
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

            logger->info("Using plugin configuration:");
            logger->info("  Game Root: {}", config.gameRoot.string());
            logger->info("  Game Locale: {}", (config.gameRoot / config.localeDir).string());
            logger->info("  Game Plugins: {}", config.gamePluginsRoot.string());
            logger->info("  User Plugins: {}", config.userPluginsRoot.string());

            if (renderThumbnailsFlag) {
                logger->info("3D thumbnail rendering enabled (Zoom 5 South, 44x44)");
            }
            ScanAndAnalyzeExemplars(config, *logger, renderThumbnailsFlag);
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
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << std::endl;
        return 1;
    }
}
