// Copied from the NAM DLL (src/cIOverrideNetworkDrag.h). Keep the two in step.
#pragma once
#include "cIGZUnknown.h"

// Public interface of the NAM DLL that other DLLs can use. Copy this header into your project.
//
// Lets the player drag an override network such as RHW-4 or a TLA directly, without placing a starter piece first.
// Start the game's drag tool of the base network (Road for a TLA, Dirt Road for the RHW, ...), then call
// SetOverrideNetwork. Every drag of that tool now turns into the override network, through the regular RUL2 override
// rules, as if a starter piece lay right behind the start of the drag. Call ClearOverrideNetwork once the tool is gone,
// or the next drags of the base network turn into the override network too.
//
// Get it from GZCOM like this. It is not there when the NAM DLL is missing or its RUL2 Engine patch is turned off.
//
//   cRZAutoRefCount<cIOverrideNetworkDrag> drag;
//   if (RZGetFrameWork()->GetCOMObject()->GetClassObject(GZCLSID_cIOverrideNetworkDrag, GZIID_cIOverrideNetworkDrag, drag.AsPPVoid())) {
//       const cIOverrideNetworkDrag::TileRow rhw4 = {0x57030000, 0, 0};
//       drag->SetOverrideNetwork(&rhw4, 1);
//   }
static const uint32_t GZCLSID_cIOverrideNetworkDrag = 0xf95d726f;
static const uint32_t GZIID_cIOverrideNetworkDrag = 0x00418ac7;

class cIOverrideNetworkDrag : public cIGZUnknown
{
public:
	// One row of tiles of the override network. A network that is one tile wide, such as RHW-4, is one row. A wider
	// one, such as RHW-8S, has one row per tile of its width, as the tiles of its starter piece lie side by side.
	// Each row is described while the network heads south.
	struct TileRow
	{
		uint32_t straightTileId;  // the orthogonal straight tile of the row, such as 0x57030000 for RHW-4
		int32_t offset;           // cells from the row that follows the cursor, to the east (left of the drag)
		uint32_t rotationFlip;    // orientation of the tile, as in RUL files: rotation 0 to 3, + 0x80 when flipped
	};

	// Drags turn into this override network from now on. The rows are copied. One row must have offset 0.
	// Rows other than the one at offset 0 only follow straight drags along the grid, not curves or diagonals.
	virtual void SetOverrideNetwork(const TileRow* rows, uint32_t rowCount) = 0;

	// Drags are plain drags of the base network again.
	virtual void ClearOverrideNetwork() = 0;
};
