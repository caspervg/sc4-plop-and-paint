#include "TextureKindCache.hpp"

#include <system_error>
#include <utility>

#include "../utils/Logger.h"
#include "rfl/cbor/load.hpp"
#include "rfl/cbor/save.hpp"

namespace decals {

TextureKindCache LoadTextureKindCache(const std::filesystem::path& path, const int64_t outsideCacheStamp) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return {};
    }

    auto result = rfl::cbor::load<TextureKindCache>(path.string());
    if (!result) {
        LOG_WARN("Could not read {}, textures will be sorted again: {}", path.string(), result.error().what());
        return {};
    }
    if (result->outsideCacheStamp != outsideCacheStamp) {
        LOG_INFO("The plugin cache was rebuilt, textures will be sorted again");
        return {};
    }
    return std::move(*result);
}

void SaveTextureKindCache(const std::filesystem::path& path, const TextureKindCache& cache) {
    if (const auto result = rfl::cbor::save(path.string(), cache); !result) {
        LOG_WARN("Could not save {}: {}", path.string(), result.error().what());
    }
}

}  // namespace decals
