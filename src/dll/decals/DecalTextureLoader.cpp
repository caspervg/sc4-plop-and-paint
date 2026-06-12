#include "DecalTextureLoader.hpp"

#include <span>
#include <utility>
#include <vector>

#include "../utils/Logger.h"
#include "FSHReader.h"
#include "cGZPersistResourceKey.h"
#include "cIGZPersistDBSegment.h"
#include "cIGZPersistResourceManager.h"
#include "cRZAutoRefCount.h"

namespace {
    // Reads and parses the FSH record, leaving the first bitmap's RGBA pixels
    // in outRGBA. Returns false (quietly for missing records) on any failure.
    bool LoadFirstBitmapRGBA(cIGZPersistResourceManager* pRM,
                             const uint32_t instanceId,
                             std::vector<uint8_t>& outRGBA,
                             uint16_t& outWidth,
                             uint16_t& outHeight) {
        if (!pRM) {
            return false;
        }

        const cGZPersistResourceKey key{decals::kDecalTextureType, decals::kDecalTextureGroup, instanceId};

        cRZAutoRefCount<cIGZPersistDBSegment> segment;
        if (!pRM->FindDBSegment(key, segment.AsPPObj()) || !segment) {
            LOG_DEBUG("DecalTextureLoader: no segment for texture 0x{:08X}", instanceId);
            return false;
        }

        const uint32_t size = segment->GetRecordSize(key);
        if (size == 0) {
            LOG_WARN("DecalTextureLoader: zero-size record for texture 0x{:08X}", instanceId);
            return false;
        }

        std::vector<uint8_t> buf(size);
        uint32_t readSize = size;
        if (segment->ReadRecord(key, buf.data(), readSize) == 0) {
            LOG_WARN("DecalTextureLoader: failed to read record for texture 0x{:08X}", instanceId);
            return false;
        }

        auto parseResult = FSH::Reader::Parse(std::span<const uint8_t>(buf.data(), readSize));
        if (!parseResult) {
            LOG_WARN("DecalTextureLoader: FSH parse failed for texture 0x{:08X}", instanceId);
            return false;
        }

        const FSH::Record& record = *parseResult;
        if (record.entries.empty() || record.entries[0].bitmaps.empty()) {
            LOG_WARN("DecalTextureLoader: FSH has no bitmaps for texture 0x{:08X}", instanceId);
            return false;
        }

        // Use the first (highest-resolution) bitmap from the first entry
        const FSH::Bitmap& bitmap = record.entries[0].bitmaps[0];
        if (!FSH::Reader::ConvertToRGBA8(bitmap, outRGBA)) {
            LOG_WARN("DecalTextureLoader: RGBA conversion failed for texture 0x{:08X}", instanceId);
            return false;
        }

        outWidth = bitmap.width;
        outHeight = bitmap.height;
        return true;
    }
}

namespace decals {

bool LoadDecalTexture(cIGZPersistResourceManager* pRM,
                      cIGZImGuiService* imguiService,
                      const uint32_t instanceId,
                      ImGuiTexture& outTexture,
                      ImVec2& outSourceSize) {
    if (!pRM || !imguiService) {
        return false;
    }

    std::vector<uint8_t> rgba;
    uint16_t width = 0;
    uint16_t height = 0;
    if (!LoadFirstBitmapRGBA(pRM, instanceId, rgba, width, height)) {
        return false;
    }

    // ImGuiTexture::Create expects BGRA8 - swap R and B channels
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        std::swap(rgba[i], rgba[i + 2]);
    }

    if (!outTexture.Create(imguiService, width, height, rgba.data())) {
        LOG_WARN("DecalTextureLoader: texture upload failed for texture 0x{:08X}", instanceId);
        return false;
    }

    outSourceSize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    return true;
}

TextureKind ClassifyDecalTexture(cIGZPersistResourceManager* pRM, const uint32_t instanceId) {
    std::vector<uint8_t> rgba;
    uint16_t width = 0;
    uint16_t height = 0;
    if (!LoadFirstBitmapRGBA(pRM, instanceId, rgba, width, height)) {
        return TextureKind::Unknown;
    }

    for (size_t i = 3; i < rgba.size(); i += 4) {
        if (rgba[i] != 0xFF) {
            return TextureKind::Overlay;
        }
    }
    return TextureKind::Base;
}

}  // namespace decals
