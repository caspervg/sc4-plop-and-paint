#pragma once

#include <Windows.h>

#include <cstdint>

enum class SC4UserDirFixStatus : uint8_t {
    Applied,
    AlreadyApplied,
    NoUserDirArgument,
    UnsupportedVersion,
    ModuleNotFound,
    PathConversionFailed,
    ProtectFailed,
};

[[nodiscard]] SC4UserDirFixStatus ApplySC4UserDirFix(HINSTANCE moduleInstance) noexcept;
