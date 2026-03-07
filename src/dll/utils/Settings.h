#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include <spdlog/common.h>

#include "paint/PaintSettings.hpp"

class Settings {
public:
    Settings();

    void Load(const std::filesystem::path& settingsFilePath);

    // Logging
    [[nodiscard]] spdlog::level::level_enum GetLogLevel() const noexcept;
    [[nodiscard]] bool GetLogToFile() const noexcept;
    [[nodiscard]] bool GetEnableDrawOverlay() const noexcept;
    [[nodiscard]] PreviewMode GetDefaultPropPreviewMode() const noexcept;
    [[nodiscard]] bool GetDefaultShowGridOverlay() const noexcept;
    [[nodiscard]] bool GetDefaultSnapPointsToGrid() const noexcept;
    [[nodiscard]] bool GetDefaultSnapPlacementsToGrid() const noexcept;
    [[nodiscard]] float GetDefaultGridStepMeters() const noexcept;

private:
    spdlog::level::level_enum logLevel_;
    bool logToFile_;
    bool enableDrawOverlay_;
    PreviewMode defaultPropPreviewMode_;
    bool defaultShowGridOverlay_;
    bool defaultSnapPointsToGrid_;
    bool defaultSnapPlacementsToGrid_;
    float defaultGridStepMeters_;
};
