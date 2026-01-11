#include <args.hxx>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "shared/ReflectEntities.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#ifndef SC4_ADVANCED_LOT_PLOP_VERSION
#define SC4_ADVANCED_LOT_PLOP_VERSION "0.0.0"
#endif

namespace fs = std::filesystem;

namespace {

fs::path DefaultPluginsFolder()
{
#ifdef _WIN32
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        return fs::path(userProfile) / "Documents" / "SimCity 4" / "Plugins";
    }
#endif

    if (const char* home = std::getenv("HOME")) {
        return fs::path(home) / ".simcity4" / "plugins";
    }

    return fs::current_path();
}

void ListPluginContents(const fs::path& folder, spdlog::logger& logger)
{
    if (!fs::exists(folder)) {
        logger.warn("Plugins directory '{}' does not exist", folder.string());
        return;
    }

    try {
        for (const auto& entry : fs::directory_iterator(folder)) {
            const auto label = entry.is_directory() ? "dir" : "file";
            logger.info("  [{}] {}", label, entry.path().filename().string());
        }
    } catch (const fs::filesystem_error& error) {
        logger.warn("Failed to enumerate '{}': {}", folder.string(), error.what());
    }
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        auto logger = spdlog::stdout_color_mt("lotplop-cli");
        spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
        logger->info("SC4AdvancedLotPlop CLI {}", SC4_ADVANCED_LOT_PLOP_VERSION);

        const auto sampleLot = sc4::shared::LotMetadata::CreateSample();
        logger->info("Mock lot sample: id={} name=\"{}\" density={:.2f}",
                     sampleLot.lotId,
                     sampleLot.lotName,
                     sampleLot.buildDensity);
        const auto fieldNames = sc4::shared::LotMetadata::FieldNames();
        logger->info("Reflect field names: {}, {}, {}",
                     fieldNames[0],
                     fieldNames[1],
                     fieldNames[2]);
        const auto clonedLot =
            sc4::shared::LotMetadata::FromTuple(sampleLot.AsTuple());
        logger->info("Cloned lot via reflect: {} ({})",
                     clonedLot.lotName,
                     clonedLot.lotId);

        const fs::path pluginsFolder = DefaultPluginsFolder();

        args::ArgumentParser parser("SC4AdvancedLotPlop CLI", "Inspect and log information about SimCity 4 plugin folders.");
        args::HelpFlag helpFlag(parser, "help", "Show this help message", {'h', "help"});
        args::Flag versionFlag(parser, "version", "Print the bundled DLL version", {"version"});
        args::Flag listFlag(parser, "list", "List the default Plugins folder contents", {"list"});
        args::Flag pathFlag(parser, "path", "Print the default Plugins folder path", {"path"});

        try {
            parser.ParseCLI(argc, argv);
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

        if (listFlag) {
            ListPluginContents(pluginsFolder, *logger);
            return 0;
        }

        if (pathFlag) {
            logger->info("Plugins directory: {}", pluginsFolder.string());
            return 0;
        }

        logger->info("Plugins directory: {}", pluginsFolder.string());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << std::endl;
        return 1;
    }
}
