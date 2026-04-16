# Terrain Decal Clipping Integration

This note records the terrain decal hook that is currently implemented in
`src/dll/terrain/`.

The goal is to stop clamped terrain decals from smearing their edge texels out
to the integer terrain rect chosen by vanilla `cSTEOverlayManager::DrawRect`.

## Problem Summary

The stock terrain decal path is:

1. `cSTEOverlayManager::UpdateDecal(...)` computes an integer terrain rect from
   the decal footprint.
2. `cSTEOverlayManager::DrawDecals(...)` binds texture state and the decal
   transform.
3. `cSTEOverlayManager::DrawRect(...)` redraws the terrain mesh subset for that
   rect.

The terrain geometry itself is not clipped to the decal footprint. For clamped
axes, the outer texel is stretched to the rect boundary.

## Current Hook Model

The plugin does not detour `DrawDecals(...)` or `DrawRect(...)` globally.
Instead it patches the x86 `DrawDecals -> DrawRect` call site for the supported
Windows build.

For SimCity 4 `1.1.641` on Windows x86:

- `cSTEOverlayManager::DrawDecals = 0x00736790`
- `cSTEOverlayManager::DrawRect = 0x00735720`
- `DrawRect` call site inside `DrawDecals = 0x00736B88`

The patched call site has this shape:

```asm
00736B81  ADD ESI,0x0C
00736B84  PUSH ESI
00736B85  PUSH EBX
00736B86  MOV  ECX,EBP
00736B88  CALL 0x00735720
```

That lets the hook:

- keep vanilla overlay iteration and render-state setup intact
- recover the current overlay slot from `rect - 0x0C`
- replace only the terrain geometry submission for supported decals
- fall through to vanilla `DrawRect(...)` for unsupported cases

This is the API shape used by the current renderer:

```cpp
TerrainDecal::DrawRequest {
    void* overlayManager;
    SC4DrawContext* drawContext;
    const cRZRect* rect;
    const std::byte* overlaySlotBase;
    std::ptrdiff_t overlayRectOffset;
    const HookAddresses* addresses;
    cISTETerrain* terrain;
    cISTETerrainView* terrainView;
};

enum class DrawResult {
    FallThroughToVanilla,
    Handled,
};
```

## Renderer That Is Actually Implemented

`ClippedTerrainDecalRenderer::Draw(...)` currently does this:

1. Recovers the active overlay slot from `rectPtr - 0x0C`.
2. Reads the overlay state, flags, rect, and raw slot matrix.
3. Rejects unsupported slot states and unsupported terrain data early.
4. Walks the terrain cells covered by the clamped draw rect.
5. Loads each cell as a 4-corner terrain quad.
6. Evaluates the decal footprint in world space using the raw slot matrix.
7. Clips the full cell polygon against the active `[0, 1]` clip box on the
   clamped axes.
8. Emits explicit triangles for the surviving polygon.
9. Submits those triangles through `SC4DrawContext::DrawPrims(...)`.

The key point is that geometry outside the decal footprint is omitted entirely.
Nothing outside the footprint is hidden with transparency or border color tricks.

## Footprint Space

The working clip-domain evaluation is:

```cpp
u = x * m[0] + y * m[4] + z * m[8]  + m[12];
v = x * m[1] + y * m[5] + z * m[9]  + m[13];
w = x * m[3] + y * m[7] + z * m[11] + m[15];
```

with perspective divide only when `w` is meaningfully different from `0` and
`1`.

Important: this is evaluated from world `x/y/z` against the raw slot matrix.

The following approaches were tested and discarded:

- clipping against the captured `SetTexTransform4(...)` matrix
- clipping in source terrain `u/v`
- re-normalizing from `cDecalInfo`

Those paths did not match the footprint correctly.

## Terrain Cell Reconstruction

The renderer loads terrain geometry in this order:

1. Preferred path: prepared per-row cell vertices from
   `terrainPreparedCellVerticesRowsPtr` (`0x00B4C6B0` on 1.1.641).
2. Fallback path: raw terrain vertex grid plus cell-info rows.

The fallback path mirrors the vanilla prepared-row builder closely enough for
the decal renderer:

- base cell corners come from the raw terrain vertex grid
- the four corners are read as one terrain quad
- when a cell-info row is available, the fallback overwrites all four `y`
  values from `flatYBits`

That `y` overwrite matters because the decal pass uses generated texcoords from
vertex position, not just authored UVs.

## `DrawPrims` Findings

The final visible fix was not more clip math. It was using the correct SC4
primitive type when submitting the already-clipped geometry.

What is now implemented:

- `vertexFormat = 0x0B`
- `primType = 0`
- `vertexCount = outputVertices.size()`
- `vertexPtr = outputVertices.data()`

This matches the explicit-triangle caller pattern seen elsewhere in the game.

### Primitive-Type Conclusion

The useful Ghidra findings were:

- `cSC4SignpostOccupant::Draw` uses `DrawPrims(..., 6, ..., 4, ptr)` for
  4-vertex quads.
- `cSTETerrainView3D::DrawPlumbingInfo` uses `DrawPrims(..., 0, ...,
  vertexCount, ptr)` when it builds explicit triangles in client memory.

So for the clipped terrain decal renderer:

- `primType = 6` is the stock quad-style client-vertex path
- `primType = 0` is the correct explicit-triangle path

Two rejected dead ends:

- `primType = 4` produced the wrong visual result for explicit clipped geometry
- `primType = 6` is not a drop-in replacement for an arbitrary triangle stream

## Texture-Coordinate Source Findings

The decal pass does not simply sample vertex UV set 0 or UV set 1.

Verified x86 behavior:

- `cSTEOverlayManager::DrawDecals(...)` calls
  `SC4DrawContext::SetTexTransform4(..., stage = 0)`
- `SC4DrawContext::SetTexTransform4(...)` calls
  `SetTexCoord(this, 0x10, stage)`

For the decal path that means:

- stage 0 uses `SetTexCoord(..., 0x10, 0)`

That matches the SCGL interpretation of `0x10`-family sources as
camera-space/generated position-style texcoords.

Practical takeaway:

- swapping emitted `u/v` versus `extra0/extra1` is not the core fix
- the important inputs are the clipped positions and the primitive submission

## Current Address Table

For SimCity 4 `1.1.641` Windows x86, the hook currently uses:

- `DrawDecals = 0x00736790`
- `DrawRect = 0x00735720`
- `DrawRectCallSite = 0x00736B88`
- `SetVertexBuffer = 0x007D2970`
- `DrawPrims = 0x007D2990`
- `DrawPrimsIndexed = 0x007D29C0`
- `DrawPrimsIndexedRaw = 0x007D29F0`
- `DrawTerrainMeshSubsetInDrawFrustum = 0x007541C0`
- `DrawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum = 0x007545C0`
- `terrainGridVerticesPtr = 0x00B4C758`
- `terrainCellInfoRowsPtr = 0x00B4C6AC`
- `terrainPreparedCellVerticesRowsPtr = 0x00B4C6B0`
- `terrainCellCountXPtr = 0x00B4C744`
- `terrainCellCountZPtr = 0x00B4C748`
- `terrainVertexCountXPtr = 0x00B4C74C`
- `terrainVertexCountPtr = 0x00B4C754`
- `overlayRectOffset = 0x0C`

Treat these as build-specific observations for the supported executable, not
universal SC4 constants.

## Kept Design Choices

These are intentional parts of the final renderer and should not be "cleaned up"
without re-verifying the whole path:

- call-site patch instead of a global `DrawRect(...)` hook
- world-space footprint evaluation from the raw slot matrix
- prepared-row terrain fetch first, raw-grid fallback second
- raw-grid fallback `y` override from `flatYBits`
- full-quad clipping against the footprint box
- explicit triangle submission with `primType = 0`

## Discarded Debug/Prototype Paths

These were useful during reverse engineering but are not part of the final
design:

- `TerrainDecalExamples.*`
- example command generation / `DrawCommands(...)`
- `SetTexTransform4(...)` capture plumbing
- clipping in terrain `u/v`
- `cDecalInfo`-based footprint normalization
- primitive submissions using `primType = 4` or `primType = 6`

## Expected Result

The clipped decal renderer should keep the decal confined to its actual
footprint instead of stretching edge texels out to the terrain-rect boundary.

That is the behavior the current implementation now achieves.
