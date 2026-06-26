#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cGZPersistResourceKey.h"

class cIGZIStream;
class cIGZOStream;
class cIGZPersistDBSegment;

namespace lottex {

// One persisted lot-texture removal. Lot base/overlay textures are regenerated
// from the shared lot configuration on every city load, so a strip must be
// re-applied after load; this record carries everything needed to redo it.
// Matched back to the lot by its anchor cell (cISC4Lot::GetLocation).
struct StripRecord {
    int32_t  lotCellX{0};
    int32_t  lotCellZ{0};
    // cISC4LotConfiguration::GetID of the lot when stripped. Guards against a
    // different lot occupying the same anchor cell after demolish/redevelop.
    uint32_t lotConfigID{0};
    uint32_t normalizedIID{0};
    float    minX{0.0f};
    float    minZ{0.0f};
    float    maxX{0.0f};
    float    maxZ{0.0f};
};

// Persisted as a private TGI subfile inside the city's DBPF save (same approach
// sc4-render-services uses for terrain decals), so strips travel with the save.
namespace sidecar {
    constexpr uint32_t FourCC(const char a, const char b, const char c, const char d) noexcept {
        return static_cast<uint32_t>(static_cast<uint8_t>(a))
             | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
             | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16)
             | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
    }

    constexpr uint32_t kMagic = FourCC('L', 'T', 'S', 'D');
    constexpr uint16_t kVersionMajor = 1;
    constexpr uint16_t kVersionMinor = 0;
    constexpr uint32_t kChunkTag = FourCC('L', 'T', 'S', 'R');

    constexpr uint32_t kType = 0xE5C2B9A9u;
    constexpr uint32_t kGroup = FourCC('L', 'T', 'S', 'D');
    constexpr uint32_t kInstance = 0x00000001u;
    constexpr cGZPersistResourceKey kKey(kType, kGroup, kInstance);

    struct ReadResult {
        bool ok{false};
        std::string error{};
        std::vector<StripRecord> records{};
    };

    [[nodiscard]] ReadResult Read(cIGZIStream& in);
    bool Write(cIGZOStream& out, const std::vector<StripRecord>& records);
    bool DeleteRecord(cIGZPersistDBSegment* dbSegment);
}

}  // namespace lottex
