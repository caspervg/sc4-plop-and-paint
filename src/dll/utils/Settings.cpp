#include "Settings.h"
#include "Logger.h"

#include "mini/ini.h"

#include "../common/Constants.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <string>

namespace {
    constexpr spdlog::level::level_enum kDefaultLogLevel = spdlog::level::info;
    constexpr bool kDefaultLogToFile = true;
    constexpr bool kDefaultEnableDrawOverlay = true;
    constexpr auto kDefaultPropPreviewMode = PreviewMode::Combined;
    constexpr bool kDefaultShowGridOverlay = true;
    constexpr bool kDefaultSnapPointsToGrid = false;
    constexpr bool kDefaultSnapPlacementsToGrid = false;
    constexpr float kDefaultGridStepMeters = 16.0f;
    constexpr float kDefaultThumbnailDisplaySize = 44.0f;
    constexpr std::array<uint8_t, 4> kDefaultThumbnailBackgroundColor = {0x42, 0x50, 0x66, 0xFF};
    constexpr std::array<uint8_t, 4> kDefaultThumbnailBorderColor = {0x5B, 0x6B, 0x84, 0xFF};
    constexpr bool kDefaultEnableRecentPaints = true;
    constexpr size_t kDefaultRecentPaintMaxItems = 8;
    constexpr auto kDefaultPaintSwitchPolicy = PaintSwitchPolicy::KeepPending;

    const std::string kSectionName = "SC4PlopAndPaint";

    std::string ToLower(const std::string& value) {
        std::string normalized(value);
        std::ranges::transform(normalized, normalized.begin(),
                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return normalized;
    }

    spdlog::level::level_enum ParseLogLevel(const std::string& value, bool& valid) {
        const std::string normalized = ToLower(value);

        if (normalized == "trace") { valid = true; return spdlog::level::trace; }
        if (normalized == "debug") { valid = true; return spdlog::level::debug; }
        if (normalized == "info") { valid = true; return spdlog::level::info; }
        if (normalized == "warn" || normalized == "warning") { valid = true; return spdlog::level::warn; }
        if (normalized == "error") { valid = true; return spdlog::level::err; }
        if (normalized == "critical") { valid = true; return spdlog::level::critical; }
        if (normalized == "off") { valid = true; return spdlog::level::off; }

        valid = false;
        return kDefaultLogLevel;
    }

    bool ParseBool(const std::string& value, bool& valid) {
        const std::string normalized = ToLower(value);

        if (normalized == "true" || normalized == "1" || normalized == "yes") {
            valid = true;
            return true;
        }
        if (normalized == "false" || normalized == "0" || normalized == "no") {
            valid = true;
            return false;
        }

        valid = false;
        return false;
    }

    PreviewMode ParsePropPreviewMode(const std::string& value, bool& valid) {
        const std::string normalized = ToLower(value);

        if (normalized == "outline" || normalized == "outlineonly") {
            valid = true;
            return PreviewMode::Outline;
        }
        if (normalized == "full" || normalized == "fullprop" || normalized == "fullmodel") {
            valid = true;
            return PreviewMode::FullModel;
        }
        if (normalized == "combined" || normalized == "both" || normalized == "outlineandfull") {
            valid = true;
            return PreviewMode::Combined;
        }

        valid = false;
        return kDefaultPropPreviewMode;
    }

    float ParseFloat(const std::string& value, bool& valid) {
        try {
            size_t parsedChars = 0;
            const float parsed = std::stof(value, &parsedChars);
            valid = parsedChars == value.size() && std::isfinite(parsed);
            return parsed;
        }
        catch (...) {
            valid = false;
            return 0.0f;
        }
    }

    size_t ParseSizeT(const std::string& value, bool& valid) {
        try {
            size_t parsedChars = 0;
            const auto parsed = std::stoul(value, &parsedChars);
            valid = parsedChars == value.size();
            return valid ? static_cast<size_t>(parsed) : 0;
        }
        catch (...) {
            valid = false;
            return 0;
        }
    }

    PaintSwitchPolicy ParsePaintSwitchPolicy(const std::string& value, bool& valid) {
        const std::string normalized = ToLower(value);

        if (normalized == "discard") {
            valid = true;
            return PaintSwitchPolicy::Discard;
        }
        if (normalized == "commit") {
            valid = true;
            return PaintSwitchPolicy::Commit;
        }
        if (normalized == "keep") {
            valid = true;
            return PaintSwitchPolicy::KeepPending;
        }

        valid = false;
        return kDefaultPaintSwitchPolicy;
    }

    std::array<uint8_t, 4> ParseHexColor(const std::string& value, bool& valid) {
        std::string normalized;
        normalized.reserve(value.size());

        for (const unsigned char c : value) {
            if (!std::isspace(c)) {
                normalized.push_back(static_cast<char>(c));
            }
        }

        if (!normalized.empty() && normalized.front() == '#') {
            normalized.erase(normalized.begin());
        }

        if (normalized.empty()) {
            valid = true;
            return {0, 0, 0, 0};
        }

        if (normalized.size() != 6 && normalized.size() != 8) {
            valid = false;
            return kDefaultThumbnailBackgroundColor;
        }

        if (!std::ranges::all_of(normalized, [](const unsigned char c) { return std::isxdigit(c) != 0; })) {
            valid = false;
            return kDefaultThumbnailBackgroundColor;
        }

        try {
            const auto parseComponent = [&normalized](const size_t offset) {
                return static_cast<uint8_t>(std::stoul(normalized.substr(offset, 2), nullptr, 16));
            };

            valid = true;
            return {
                parseComponent(0),
                parseComponent(2),
                parseComponent(4),
                normalized.size() == 8 ? parseComponent(6) : static_cast<uint8_t>(255)
            };
        }
        catch (...) {
            valid = false;
            return kDefaultThumbnailBackgroundColor;
        }
    }

} // namespace

Settings::Settings()
    : logLevel_(kDefaultLogLevel)
    , logToFile_(kDefaultLogToFile)
    , enableDrawOverlay_(kDefaultEnableDrawOverlay)
    , defaultPropPreviewMode_(kDefaultPropPreviewMode)
    , defaultShowGridOverlay_(kDefaultShowGridOverlay)
    , defaultSnapPointsToGrid_(kDefaultSnapPointsToGrid)
    , defaultSnapPlacementsToGrid_(kDefaultSnapPlacementsToGrid)
    , defaultGridStepMeters_(kDefaultGridStepMeters)
    , thumbnailDisplaySize_(kDefaultThumbnailDisplaySize)
    , thumbnailBackgroundColor_(kDefaultThumbnailBackgroundColor)
    , thumbnailBorderColor_(kDefaultThumbnailBorderColor)
    , enableRecentPaints_(kDefaultEnableRecentPaints)
    , recentPaintMaxItems_(kDefaultRecentPaintMaxItems)
    , paintSwitchPolicy_(kDefaultPaintSwitchPolicy) {}

void Settings::Load(const std::filesystem::path& settingsFilePath) {
    // Reset to defaults
    *this = Settings();

    try {
        const mINI::INIFile file(settingsFilePath.string());
        mINI::INIStructure ini;

        if (!file.read(ini)) {
            LOG_INFO("Using default settings, no configuration file detected: {}", settingsFilePath.string());
            return;
        }

        if (!ini.has(kSectionName)) {
            LOG_INFO("Using default settings, section [{}] missing in {}", kSectionName, settingsFilePath.string());
            return;
        }
        auto section = ini.get(kSectionName);

        // LogLevel
        if (section.has("LogLevel")) {
            bool valid = false;
            const std::string text = section.get("LogLevel");
            logLevel_ = ParseLogLevel(text, valid);
            if (!valid) {
                logLevel_ = kDefaultLogLevel;
                LOG_ERROR("Invalid LogLevel value '{}' in {}. Using default info.", text, settingsFilePath.string());
            }
        }

        // LogToFile
        if (section.has("LogToFile")) {
            bool valid = false;
            const std::string text = section.get("LogToFile");
            logToFile_ = ParseBool(text, valid);
            if (!valid) {
                logToFile_ = kDefaultLogToFile;
                LOG_ERROR("Invalid LogToFile value '{}' in {}. Using default true.", text, settingsFilePath.string());
            }
        }

        // EnableDrawOverlay
        if (section.has("EnableDrawOverlay")) {
            bool valid = false;
            const std::string text = section.get("EnableDrawOverlay");
            enableDrawOverlay_ = ParseBool(text, valid);
            if (!valid) {
                enableDrawOverlay_ = kDefaultEnableDrawOverlay;
                LOG_ERROR("Invalid EnableDrawOverlay value '{}' in {}. Using default true.", text,
                          settingsFilePath.string());
            }
        }

        if (section.has("DefaultPropPreviewMode")) {
            bool valid = false;
            const std::string text = section.get("DefaultPropPreviewMode");
            defaultPropPreviewMode_ = ParsePropPreviewMode(text, valid);
            if (!valid) {
                defaultPropPreviewMode_ = kDefaultPropPreviewMode;
                LOG_ERROR("Invalid DefaultPropPreviewMode value '{}' in {}. Using default outline.", text,
                          settingsFilePath.string());
            }
        }

        if (section.has("DefaultShowGridOverlay")) {
            bool valid = false;
            const std::string text = section.get("DefaultShowGridOverlay");
            defaultShowGridOverlay_ = ParseBool(text, valid);
            if (!valid) {
                defaultShowGridOverlay_ = kDefaultShowGridOverlay;
                LOG_ERROR("Invalid DefaultShowGridOverlay value '{}' in {}. Using default true.", text,
                          settingsFilePath.string());
            }
        }

        if (section.has("DefaultSnapPointsToGrid")) {
            bool valid = false;
            const std::string text = section.get("DefaultSnapPointsToGrid");
            defaultSnapPointsToGrid_ = ParseBool(text, valid);
            if (!valid) {
                defaultSnapPointsToGrid_ = kDefaultSnapPointsToGrid;
                LOG_ERROR("Invalid DefaultSnapPointsToGrid value '{}' in {}. Using default false.", text,
                          settingsFilePath.string());
            }
        }

        if (section.has("DefaultSnapPlacementsToGrid")) {
            bool valid = false;
            const std::string text = section.get("DefaultSnapPlacementsToGrid");
            defaultSnapPlacementsToGrid_ = ParseBool(text, valid);
            if (!valid) {
                defaultSnapPlacementsToGrid_ = kDefaultSnapPlacementsToGrid;
                LOG_ERROR("Invalid DefaultSnapPlacementsToGrid value '{}' in {}. Using default false.", text,
                          settingsFilePath.string());
            }
        }

        if (section.has("DefaultGridStepMeters")) {
            bool valid = false;
            const std::string text = section.get("DefaultGridStepMeters");
            const float parsed = ParseFloat(text, valid);
            if (!valid || parsed < 1.0f) {
                defaultGridStepMeters_ = kDefaultGridStepMeters;
                LOG_ERROR("Invalid DefaultGridStepMeters value '{}' in {}. Using default 16.0.", text,
                          settingsFilePath.string());
            }
            else {
                defaultGridStepMeters_ = parsed;
            }
        }

        if (section.has("ThumbnailDisplaySize")) {
            bool valid = false;
            const std::string text = section.get("ThumbnailDisplaySize");
            const float parsed = ParseFloat(text, valid);
            if (!valid || parsed < UI::kMinIconSize || parsed > UI::kMaxIconSize) {
                thumbnailDisplaySize_ = kDefaultThumbnailDisplaySize;
                LOG_ERROR("Invalid ThumbnailDisplaySize value '{}' in {}. Using default 44.0.", text,
                          settingsFilePath.string());
            }
            else {
                thumbnailDisplaySize_ = parsed;
            }
        }

        if (section.has("ThumbnailBackgroundColor")) {
            bool valid = false;
            const std::string text = section.get("ThumbnailBackgroundColor");
            thumbnailBackgroundColor_ = ParseHexColor(text, valid);
            if (!valid) {
                thumbnailBackgroundColor_ = kDefaultThumbnailBackgroundColor;
                LOG_ERROR(
                    "Invalid ThumbnailBackgroundColor value '{}' in {}. Using default 425066.",
                    text,
                    settingsFilePath.string());
            }
        }

        if (section.has("ThumbnailBorderColor")) {
            bool valid = false;
            const std::string text = section.get("ThumbnailBorderColor");
            thumbnailBorderColor_ = ParseHexColor(text, valid);
            if (!valid) {
                thumbnailBorderColor_ = kDefaultThumbnailBorderColor;
                LOG_ERROR(
                    "Invalid ThumbnailBorderColor value '{}' in {}. Using default 5B6B84.",
                    text,
                    settingsFilePath.string());
            }
        }

        if (section.has("EnableRecentPaints")) {
            bool valid = false;
            const std::string text = section.get("EnableRecentPaints");
            enableRecentPaints_ = ParseBool(text, valid);
            if (!valid) {
                enableRecentPaints_ = kDefaultEnableRecentPaints;
                LOG_ERROR("Invalid EnableRecentPaints value '{}' in {}. Using default true.", text,
                          settingsFilePath.string());
            }
        }

        if (section.has("RecentPaintMaxItems")) {
            bool valid = false;
            const std::string text = section.get("RecentPaintMaxItems");
            const size_t parsed = ParseSizeT(text, valid);
            if (!valid || parsed < 1 || parsed > 16) {
                recentPaintMaxItems_ = kDefaultRecentPaintMaxItems;
                LOG_ERROR("Invalid RecentPaintMaxItems value '{}' in {}. Using default 8.", text,
                          settingsFilePath.string());
            }
            else {
                recentPaintMaxItems_ = parsed;
            }
        }

        if (section.has("PaintSwitchPolicy")) {
            bool valid = false;
            const std::string text = section.get("PaintSwitchPolicy");
            paintSwitchPolicy_ = ParsePaintSwitchPolicy(text, valid);
            if (!valid) {
                paintSwitchPolicy_ = kDefaultPaintSwitchPolicy;
                LOG_ERROR("Invalid PaintSwitchPolicy value '{}' in {}. Using default keep.", text,
                          settingsFilePath.string());
            }
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error reading settings file {}: {}", settingsFilePath.string(), e.what());
        *this = Settings();
    }
}

spdlog::level::level_enum Settings::GetLogLevel() const noexcept { return logLevel_; }
bool Settings::GetLogToFile() const noexcept { return logToFile_; }
bool Settings::GetEnableDrawOverlay() const noexcept { return enableDrawOverlay_; }
PreviewMode Settings::GetDefaultPropPreviewMode() const noexcept { return defaultPropPreviewMode_; }
bool Settings::GetDefaultShowGridOverlay() const noexcept { return defaultShowGridOverlay_; }
bool Settings::GetDefaultSnapPointsToGrid() const noexcept { return defaultSnapPointsToGrid_; }
bool Settings::GetDefaultSnapPlacementsToGrid() const noexcept { return defaultSnapPlacementsToGrid_; }
float Settings::GetDefaultGridStepMeters() const noexcept { return defaultGridStepMeters_; }
float Settings::GetThumbnailDisplaySize() const noexcept { return thumbnailDisplaySize_; }
std::array<uint8_t, 4> Settings::GetThumbnailBackgroundColor() const noexcept { return thumbnailBackgroundColor_; }
std::array<uint8_t, 4> Settings::GetThumbnailBorderColor() const noexcept { return thumbnailBorderColor_; }
bool Settings::GetEnableRecentPaints() const noexcept { return enableRecentPaints_; }
size_t Settings::GetRecentPaintMaxItems() const noexcept { return recentPaintMaxItems_; }
PaintSwitchPolicy Settings::GetPaintSwitchPolicy() const noexcept { return paintSwitchPolicy_; }
