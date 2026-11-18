#pragma once
#include <array>
#include <cstdint>

namespace decals {

inline constexpr uint32_t kLotTextureGroup = 0x0986135E;
inline constexpr uint32_t kNetworkTextureGroup = 0x1ABE787D;

// The game's four sidewalks (zoom 4), with network textures. They go under a road texture where
// the neighbouring lot has some wealth.
struct SidewalkTexture {
    uint32_t instance;
    const char* name;
};
inline constexpr std::array<SidewalkTexture, 4> kSidewalkTextures{{
    {0x08100004u, "Sidewalk, low wealth"},
    {0x08200004u, "Sidewalk, medium wealth"},
    {0x08300004u, "Sidewalk, high wealth"},
    {0x08400004u, "Sidewalk, rural"},
}};

// 0x08n0000z with n 1 to 4. No lot texture uses these instances.
constexpr bool IsSidewalkTexture(const uint32_t instance) {
    const uint32_t wealth = (instance >> 20) & 0xFu;
    return (instance & 0xFF0FFFF0u) == 0x08000000u && wealth >= 1 && wealth <= 4;
}

// "Sidewalk, ..." for a sidewalk, null for any other texture.
constexpr const char* SidewalkName(const uint32_t instance) {
    for (const SidewalkTexture& sidewalk : kSidewalkTextures) {
        if (sidewalk.instance == instance) {
            return sidewalk.name;
        }
    }
    return nullptr;
}

// Paint and decal textures are lot textures, except the sidewalks.
constexpr uint32_t TextureGroupOf(const uint32_t instance) {
    return IsSidewalkTexture(instance) ? kNetworkTextureGroup : kLotTextureGroup;
}

static_assert(IsSidewalkTexture(0x08100004u) && IsSidewalkTexture(0x08400000u));
static_assert(!IsSidewalkTexture(0x08500004u) && !IsSidewalkTexture(0x08000004u) && !IsSidewalkTexture(0x18100004u));

}  // namespace decals
