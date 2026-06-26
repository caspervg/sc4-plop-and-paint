#pragma once

#include <cstdint>
#include <vector>

#include "LotTextureStripSidecar.hpp"

class cISC4City;

// Removal of individual lot base/overlay textures by editing the per-lot
// cISC4LotBaseTextureOccupant spec vector. Full RE writeup, Windows addresses
// and the spec layout: docs/lot-texture-stripping.md.
namespace lottex {

// Local mirror of the opaque cISC4LotBaseTextureOccupant::LotBaseTextureSpecification.
// Layout recovered from cSC4LotBaseTextureOccupant::SetTextureSpecification
// (Mac 0x000845a0 / Windows 0x005e0b20). Element size is 0x10.
struct LotTextureSpec {
    uint32_t iid;          // 0x00 resolved texture IID (zoom nibble usually 0)
    uint8_t  cellX;        // 0x04 global city cell X
    uint8_t  cellY;        // 0x05 global city cell Y
    uint8_t  orientation;  // 0x06
    uint8_t  mirrored;     // 0x07
    uint8_t  tint[4];      // 0x08 RGBA (0xFF = white/opaque)
    uint8_t  unk0C;        // 0x0C
    uint8_t  frameIndex;   // 0x0D (0xFF => random anim frame)
    uint8_t  pad0E;        // 0x0E
    uint8_t  pad0F;        // 0x0F
};
static_assert(sizeof(LotTextureSpec) == 0x10, "LotTextureSpec must be 16 bytes");

// Captured removal, enough to undo and to persist. `record` identifies the
// strip (lot anchor cell + IID + footprint) for the save sidecar; `preEditSpecs`
// is the FULL spec vector before the edit so undo can restore it wholesale
// (correct whether the removal left fewer specs or cleared the lot entirely).
struct RemovedLotTexture {
    StripRecord record{};
    std::vector<LotTextureSpec> preEditSpecs{};
    uint32_t removedCount{0};
};

// Remove every texture spec matching `normalizedIID` (compared with the low
// zoom nibble masked off) whose cell center falls inside the world rect, from
// the lot under (worldX, worldZ). On success fills `out` and returns true.
bool RemoveLotTexture(cISC4City* city,
                      float worldX, float worldZ,
                      uint32_t normalizedIID,
                      float worldMinX, float worldMinZ,
                      float worldMaxX, float worldMaxZ,
                      RemovedLotTexture& out);

// Outcome of re-applying a persisted strip after city load.
enum class StripApply {
    Applied,     // lot present and matching, texture stripped
    Skipped,     // lot present and matching, but nothing to strip (keep record)
    LotMissing,  // no lot / different lot at the anchor cell (drop the record)
};

// Re-apply a persisted strip after city load (textures regenerate from config
// on load). Locates the lot from the record's anchor cell and validates its
// config ID before stripping. Fills `out` on Applied (preEditSpecs let undo
// restore the pre-strip state for this session).
StripApply ApplyStripRecord(cISC4City* city, const StripRecord& record, RemovedLotTexture& out);

// Re-apply previously removed specs (undo). Returns true on success.
bool RestoreLotTexture(cISC4City* city, const RemovedLotTexture& removed);

}  // namespace lottex
