#pragma once
#include <cstdint>

#include "imgui.h"
#include "public/ImGuiTexture.h"

class cIGZImGuiService;
class cIGZPersistResourceManager;

namespace decals {

inline constexpr uint32_t kDecalTextureType = 0x7AB50E44;
inline constexpr uint32_t kDecalTextureGroup = 0x0986135E;

// Loads a decal/lot texture FSH record and uploads one frame (FSH entry) as an ImGui texture.
// frameCount, when given, gets how many frames the FSH has: more than one for a multi-FSH.
bool LoadDecalTexture(cIGZPersistResourceManager* pRM,
                      cIGZImGuiService* imguiService,
                      uint32_t instanceId,
                      ImGuiTexture& outTexture,
                      ImVec2& outSourceSize,
                      uint32_t frame = 0,
                      uint32_t* frameCount = nullptr);

// How many frames (FSH entries) a texture has; 1 when it cannot be read.
uint32_t CountDecalFrames(cIGZPersistResourceManager* pRM, uint32_t instanceId);

enum class TextureKind : uint8_t {
    Unknown = 0,
    Base,
    Overlay,
};

// Classifies a lot texture by its alpha channel: fully opaque means base
// texture, any real transparency means overlay (matches SC4PIM-X trueAlpha).
TextureKind ClassifyDecalTexture(cIGZPersistResourceManager* pRM, uint32_t instanceId);

}  // namespace decals
