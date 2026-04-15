#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <array>

#include <spdlog/common.h>

#include "paint/PaintSettings.hpp"

enum class PaintSwitchPolicy : uint8_t {
    Discard = 0,
    Commit = 1,
    KeepPending = 2
};

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
    [[nodiscard]] float GetThumbnailDisplaySize() const noexcept;
    [[nodiscard]] std::array<uint8_t, 4> GetThumbnailBackgroundColor() const noexcept;
    [[nodiscard]] std::array<uint8_t, 4> GetThumbnailBorderColor() const noexcept;
    [[nodiscard]] bool GetEnableRecentPaints() const noexcept;
    [[nodiscard]] size_t GetRecentPaintMaxItems() const noexcept;
    [[nodiscard]] PaintSwitchPolicy GetPaintSwitchPolicy() const noexcept;

private:
    spdlog::level::level_enum logLevel_;
    bool logToFile_;
    bool enableDrawOverlay_;
    PreviewMode defaultPropPreviewMode_;
    bool defaultShowGridOverlay_;
    bool defaultSnapPointsToGrid_;
    bool defaultSnapPlacementsToGrid_;
    float defaultGridStepMeters_;
    float thumbnailDisplaySize_;
    std::array<uint8_t, 4> thumbnailBackgroundColor_;
    std::array<uint8_t, 4> thumbnailBorderColor_;
    bool enableRecentPaints_;
    size_t recentPaintMaxItems_;
    PaintSwitchPolicy paintSwitchPolicy_;
};
