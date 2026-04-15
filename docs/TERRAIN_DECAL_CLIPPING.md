# Terrain Decal Clipping Integration

This note describes how to replace SimCity 4's rect-based terrain decal rendering with a clipped-terrain path inside a Windows x86 GZCOM DLL.

The goal is to support crisp non-full-tile terrain decals without requiring authored transparent padding around the texture.

## Problem Summary

The stock terrain decal path is:

1. `cSTEOverlayManager::UpdateDecal(...)` computes an integer terrain rect from the decal footprint.
2. `cSTEOverlayManager::DrawDecals(...)` binds the decal texture, shade state, wrap state, and texture transform.
3. `cSTEOverlayManager::DrawRect(...)` redraws the terrain mesh subset for that rect.

The terrain geometry is not clipped to the decal footprint. The footprint only affects the texture transform. For clamped axes, this causes the edge texel to smear out to the terrain-rect boundary.

`cSC4PropOccupantTerrainDecal` does not fix this. It is a persistent prop wrapper around the same `cSTEOverlayManager` overlay handle and uses the same render path.

## Confirmed RE Findings

The symbolized Mac binary confirms the terrain-decal occupant path, not just the overlay renderer:

- `cSC4PropOccupantTerrainDecal::SetAllFlags(uint32_t)` is the creation bridge from occupant state into a live terrain overlay.
- When the active/visible flag is turned on and the decal has a valid texture key and size, `SetAllFlags(...)` resolves an overlay manager from `SL::TerrainView()`.
- The manager selection is driven by fade activity:
  - active fade rate: `DynamicLand`
  - otherwise: `StaticLand`
- `SetAllFlags(...)` then calls the overlay-manager decal-creation slot, stores the returned handle in `this + 0xA8`, and immediately applies overlay alpha from `this + 0xC4`.
- `SetPosition(...)` updates the stored X/Z center at `this + 0xBC/+0xC0`, then round-trips through `DecalInfo(...)` and `UpdateDecalInfo(...)`.
- `SetDecalSize(...)` round-trips through `DecalInfo(...)` and `UpdateDecalInfo(...)`.
- `SetOpacity(...)` forwards to `SetOverlayAlpha(...)`.
- `SetOpacityFade(...)` only manages fade bookkeeping and timer registration.
- `DoMessage(...)` advances the fade, applies alpha updates, and removes or commits the overlay when the fade completes.

So `cSC4PropOccupantTerrainDecal` is not a separate rasterization path. It is a persistent state wrapper around the same rect-based terrain decal renderer.

The Windows x86 binary now confirms several terrain-view and overlay-interface details too:

- `SL::TerrainView()` returns a `cISTETerrainView`-shaped interface on x86.
- vtable slot `+0x48` on that interface is `GetOverlayManager(type)`.
- vtable slot `+0x24` is `ClearCurrentSelections()`.
- vtable slot `+0x28` is `SetAutomaticCellCursorUpdateStatus(bool)`.
- x86 callers confirm the same overlay-manager enum ordering used in the headers:
  - `0 = StaticLand`
  - `1 = StaticWater`
  - `2 = DynamicLand`
  - `3 = DynamicWater`

That removes the remaining doubt about the `cISTETerrainView` mapping for the Windows hook target.

## Integration Strategy

Patch `cSTEOverlayManager::DrawDecals(...)` with an inline trampoline on Windows x86.

Do not patch `DrawRect(...)` globally.

Reasons:

- `DrawRect(...)` is generic terrain-subset redraw logic shared by other overlay types.
- `DrawDecals(...)` is already decal-specific.
- At `DrawDecals(...)` time, the engine has already selected:
  - the terrain rect
  - the texture
  - the texture transform
  - the blend/shade state
  - the wrap mode for each axis

The replacement should keep the engine's state setup and swap only the geometry submission.

## Non-Negotiable Rendering Requirement

Outside the decal footprint, the result must be the already-rendered terrain, not transparency.

That means the patch must work by omitting decal geometry outside the valid decal domain. It must not work by:

- drawing the full rect and zeroing alpha outside the footprint
- using transparent border color as the primary fix
- relying on alpha test or blend state to "hide" the stretched area

The decal pass should only overdraw the inside of the clipped footprint. Everywhere else, the underlying terrain pass should remain visible unchanged.

This is the central reason to prefer geometry clipping over texture-authoring workarounds.

## Target Platform

This design assumes:

- Windows x86 SimCity 4
- a GZCOM DLL plugin
- inline code patching with `VirtualProtect`, `FlushInstructionCache`, and a 5-byte `JMP rel32`

That matches the existing patterns in `vendor/sc4-render-services/src/sample/DrawServiceSampleDirector.cpp`.

From the currently loaded Windows x86 image in Ghidra, the relevant function addresses are:

- `cSTEOverlayManager::DrawRect` at `0x00735720`
- `cSTEOverlayManager::DrawDecals` at `0x00736790`

Treat those as build-specific observations for the loaded executable, not universal constants for every SC4 release.

## Hook Model In A GZCOM DLL

Install the hook from your director during `PostAppInit()` or after city/render services are known to be live.

Recommended lifecycle:

1. `OnStart()`:
   - register framework hook
2. `PostAppInit()`:
   - resolve version-specific addresses
   - install the inline hook on `cSTEOverlayManager::DrawDecals`
3. `PreAppShutdown()` or `PostAppShutdown()`:
   - uninstall the hook
   - free trampolines
   - release any cached vertex data

Do not install the patch before the plugin is fully loaded and logging is available.

The current repo scaffold for this lives under `src/dll/terrain/`:

- `TerrainDecalSymbols.*`: version-gated address tables for the known Windows x86 build
- `X86RelativeCallPatch.*`: a small `CALL rel32` patch helper
- `TerrainDecalExamples.*`: explicit sample partial-tile decal commands
- `ClippedTerrainDecalRenderer.*`: the renderer-facing API and the current clipped triangle implementation
- `TerrainDecalHook.*`: the hook object installed by the director

`SC4PlopAndPaintDirector::PostAppInit()` now creates and installs the hook skeleton after logging is initialized, and `PostAppShutdown()` uninstalls it again.

## Binary Patch Type

Use an inline trampoline, not a call-site patch.

Why:

- `DrawDecals(...)` is a standalone function body.
- there may be multiple call sites
- we want one replacement for all decal rendering

Use the same pattern as `InstallInlineHook(...)` in the draw-service sample:

- copy first 5 bytes
- allocate executable trampoline
- append jump back to original function body
- patch original entry with `JMP rel32 -> hook`

Resolve one level of jump stubs first.

If you decide to patch the whole `DrawDecals(...)` entry anyway, do it for a concrete reason such as:

- needing extra per-call context that is not available at the `DrawRect` call site
- wanting one place to gather stats or stash thread-local state
- preferring a build-agnostic detour over a build-specific internal call offset

Otherwise, the x86 call-site patch described below is the better first implementation.

## x86-Specific Alternative: Patch The `DrawRect` Call Site

The currently loaded Windows x86 build also exposes a cleaner local patch point inside `DrawDecals(...)`.

At `0x00736B88`, `cSTEOverlayManager::DrawDecals` makes a direct call to `cSTEOverlayManager::DrawRect`:

```asm
00736B81  ADD ESI,0x0C       ; rect pointer = overlaySlot + 0x0C
00736B84  PUSH ESI           ; cRZRect*
00736B85  PUSH EBX           ; SC4DrawContext*
00736B86  MOV  ECX,EBP       ; cSTEOverlayManager* this
00736B88  CALL 0x00735720    ; DrawRect
```

That creates a second viable hook shape for this exact x86 build:

- patch the single 5-byte `CALL` at `0x00736B88`
- redirect it to `DrawClippedDecalRectOrVanilla(...)`
- keep the entire surrounding `DrawDecals(...)` loop untouched

This is attractive because vanilla will still handle:

- overlay iteration
- overlay-type filtering
- transparency and shade setup
- texture binding
- wrap-mode selection
- texture transform upload
- end-of-pass state restoration

Your replacement only decides how the geometry for that one decal instance gets submitted.

The current skeleton follows this x86-first approach. It patches the `DrawRect` call site and exposes a renderer API shaped like:

```cpp
TerrainDecal::DrawRequest {
    void* overlayManager;
    SC4DrawContext* drawContext;
    const cRZRect* rect;
    const std::byte* overlaySlotBase;
    std::ptrdiff_t overlayRectOffset;
};

enum class DrawResult {
    FallThroughToVanilla,
    Handled,
};

class ClippedTerrainDecalRenderer {
public:
    DrawResult Draw(const DrawRequest& request);
};
```

`DrawResult::Handled` means the renderer emitted its own clipped terrain geometry and the original `DrawRect(...)` must not run.

There is also an explicit example-command API in code now:

```cpp
auto commands = TerrainDecal::BuildExampleDrawCommands({
    .baseTileX = 128,
    .baseTileZ = 192,
    .tileSizeMeters = 16.0f,
});

renderer.DrawCommands(request, commands);
```

The built-in sample commands cover three useful test cases:

- a centered half-tile stamp inside one cell
- a narrow offset stamp near a tile edge
- a rotated stamp that crosses multiple tile boundaries

These are not rendered yet, but they codify the intended future input shape for the clipped-decals backend.

## Current Renderer Implementation

The current `ClippedTerrainDecalRenderer::Draw(...)` implementation now does real geometry clipping for the supported Windows x86 build.

It works like this:

- recover the active overlay slot from `rectPtr - 0x0C`
- read the slot flags and the stored decal transform matrix
- use the stored transform only for CPU-side clip-domain evaluation
  against the live terrain vertex `u/v` fields, not against world `x/y/z`
- preserve the original terrain vertex payload when emitting geometry
- read exact terrain grid vertices from the Windows globals backing the terrain mesh
- query `cISTETerrain::IsTriangulationFlipped(x, z)` for the actual cell diagonal
- clip the two source triangles in each affected terrain cell against the decal UV box
- emit a triangle list through `SC4DrawContext::DrawPrims(...)`
  using the verified x86 caller pattern:
  `DrawPrims(drawContext, primType, vertexFormat, vertexCount, vertexPtr)`
  with `vertexFormat = 0x0B` and the current renderer using `primType = 4`
  because it emits explicit triangles

This avoids the earlier bad fallback of relying on transparent outside areas. Cells outside the clipped footprint are simply not emitted in the decal pass, so the base terrain remains visible.

The later Ghidra pass also clarified an important semantic detail:

- x86 `FUN_00751C80` updates the terrain vertex fields at offsets `0x10/0x14`
  across the `0x20`-byte terrain vertex stride
- those are the live texture coordinates the renderer uses
- so decal clipping should be evaluated in source terrain UV space, not by applying
  the decal matrix directly to world-space position

This makes the current renderer's clipping test a better match for the actual
engine draw result than the earlier world-position approximation.

The current x86 address table includes:

- `SetVertexBuffer = 0x007D2970`
- `DrawPrims = 0x007D2990`
- `DrawPrimsIndexed = 0x007D29C0`
- `DrawPrimsIndexedRaw = 0x007D29F0`
- terrain grid vertex pointer global at `0x00B4C758`
- terrain cell-info row table pointer global at `0x00B4C6AC`
- terrain `vertexCountX` global at `0x00B4C74C`
- overlay slot rect offset corrected to `0x0C`

### `DrawPrims` Verification

The extra x86 Ghidra pass materially changed one renderer assumption.

For this build, `SC4DrawContext::DrawPrims` at `0x007D2990` is consistent with:

```cpp
DrawPrims(drawContext, primType, vertexFormat, vertexCount, vertexPtr);
```

Representative caller patterns include:

- `push verts; push 4; push 2; push 6; call 0x007D2990`
- `push verts; push 4; push 1; push 6; call 0x007D2990`
- `push verts; push 4; push 0xA; push 1; call 0x007D2990`
- `push verts; push vertexCount; push 0xA; push 6; call 0x007D2990`
- `DrawTerrainMeshSubsetInDrawFrustum` at `0x00754388` directly does
  `push vertexPtr; push vertexCount; push 0x0B; push 0x06; call 0x007D2990`
  for terrain-style client vertices

That is a better fit than the vendored `sc4-render-services` wrapper, which models
`DrawPrims` differently. For the terrain-decal hook, treat the game-binary caller
patterns as the source of truth and use `sc4-render-services` only as a reference
for patch installation and general draw-service exploration.

This also means the current clipped renderer should submit:

- `vertexFormat = 0x0B` for the 32-byte terrain-style vertex payload
- `primType = 4` for the emitted triangle list
- `vertexCount = outputVertices.size()`
- `vertexPtr = outputVertices.data()`

The direct terrain call at `0x00754388` materially improves confidence that raw
`DrawPrims(..., 0x0B, ...)` is a valid path for caller-supplied terrain-style
vertices. The remaining uncertainty is only the exact primitive type choice for
our emitted explicit triangles, not the vertex format itself.

### `SetVertexBuffer` And Indexed Draw Verification

The x86 terrain renderer also confirms the indexed terrain path:

```cpp
SetVertexBuffer(drawContext, vertexFormat, vertexCount, vertexPtr);
DrawPrimsIndexed(drawContext, primType, indexStart, indexCount);
```

Ghidra also labels `0x007D2970` as:

```cpp
SC4DrawContext::SetVertexBuffer(eGDVertexFormat, unsigned long, void const*)
```

At `0x00754300`, `DrawTerrainMeshSubsetInDrawFrustum` does:

- `push vertexPtr; push vertexCount; push 0x0B; call 0x007D2970`
- then sets the index buffer
- then `push indexCount; push indexStart; push 0x00; call 0x007D29C0`

And `0x007D29C0` decompiles as an indexed wrapper that forwards:

```cpp
DrawPrimsIndexed(drawContext, primType, indexCount, 3, indexBase + indexStart * 2);
```

where the constant `3` is the baked-in index format used by the stock terrain path.

So the DLL now carries these addresses too even though the first clipped renderer
still uses the simpler raw `DrawPrims` submit path.

### Terrain Vertex Payload Verification

The symbolized Mac `DrawTexturedTerrainQuad` helper gives a good semantic
cross-check for the terrain vertex layout:

- it copies four full `0x20`-byte terrain vertices
- it overwrites only the `u/v` fields at offsets `0x10/0x14`
- it adjusts only per-vertex alpha in the diffuse color
- it preserves the last two floats at `0x18/0x1C`

So the current clipped renderer is correct to:

- preserve and interpolate the full terrain-style payload
- treat `0x10/0x14` as the live UV fields
- leave `0x18/0x1C` untouched semantically even though their exact name is still unknown

### Recovering The Overlay Slot At The Call Site

The passed rect pointer is not the start of the overlay slot. In this x86 build:

- `rectPtr = overlaySlot + 0x0C`

So a call-site replacement can recover the full slot with:

```cpp
auto* slot = reinterpret_cast<std::uint8_t*>(rectPtr) - 0x0C;
```

From that recovered slot, the replacement can still read:

- overlay flags
- overlay rect
- stored decal transform
- texture references if needed for debugging or filtering

That means a call-site patch does not lose access to the key decal state.

### Why This May Be Better Than A Full `DrawDecals` Detour

For Windows x86 specifically, this may be the best first implementation because:

- the patch is smaller and more local
- it avoids reimplementing the `DrawDecals(...)` control flow
- it lowers the chance of state-restoration regressions
- fallback to vanilla is trivial: just call `DrawRect(...)`

### What A Call-Site Patch Still Does Not Give You

The call-site helper does not receive the original `DrawDecals(...)` matrix parameter directly.

In practice, that is still workable because:

- vanilla has already uploaded the final texture transform before the call
- the overlay slot still contains the stored decal transform built by `UpdateDecal(...)`

The expected implementation is to evaluate clipping from the per-slot stored
transform and the live terrain vertex `u/v` fields, while preserving the rest of
the terrain vertex payload.

If that proves insufficient in testing, keep the call-site patch as the geometry replacement point but add a very small `DrawDecals(...)` entry shim to stash any extra per-iteration context you need.

### Why A Global `DrawRect(...)` Patch Is Worse

The current x86 binary confirms `DrawRect(...)` is called from more than the decal pass:

- `DrawDecals(...)` at `0x00736B88`
- `FUN_00736BF0(...)`
- `FUN_00737870(...)`

The two unnamed callers are shadow-style overlay passes, not the terrain decal pass.

So a global `DrawRect(...)` detour would couple the fix to non-decal overlays and raise the regression risk immediately. The call-site patch avoids that.

## Calling Convention

Treat the original function as `__thiscall` on x86:

- `ECX = cSTEOverlayManager* this`
- stack args:
  - model-view inverse or world-to-view matrix pointer
  - `SC4DrawContext*`
  - `std::vector<uint32_t> const& overlaysToDraw`

Implement the hook entry as `__fastcall`:

- first arg receives `this`
- second dummy register arg ignored
- remaining args from stack

Pattern:

```cpp
using DrawDecalsFn = void(__thiscall*)(void* self, float* modelViewInv, void* drawContext, void* overlayVector);

static DrawDecalsFn gOrigDrawDecals = nullptr;

static void __fastcall HookDrawDecals(void* self, void*, float* modelViewInv, void* drawContext, void* overlayVector);
```

Store the trampoline as `gOrigDrawDecals`.

## Data Needed From The Concrete Overlay

From RE, each overlay slot is 200 bytes and includes:

- rect at `+0x14`
- texture transform matrix at `+0x24`
- flags at `+0x08`
- texture binding or cached texture pointer at `+0xB8/+0xC0`

`DrawDecals(...)` already reads those fields before drawing.

The clipped path should read the same slot fields and preserve the same rendering state decisions.

## Replacement Flow

Within the hook, keep the original function structure as intact as possible:

1. iterate overlay ids to draw
2. reject non-decal overlays exactly as vanilla does
3. set shade/self-lit/transparency/render state exactly as vanilla does
4. bind texture exactly as vanilla does
5. set tex filtering exactly as vanilla does
6. determine wrap mode for U and V exactly as vanilla does
7. set tex transform exactly as vanilla does
8. replace `DrawRect(...)` with `DrawClippedDecalRect(...)` for supported land decals
9. otherwise fall back to vanilla `DrawRect(...)`

Do not change:

- alpha handling
- color handling
- shadow draw passes
- stats counters
- default render-state restoration at the end

Do not reinterpret the design as an alpha-masking pass. The clipped replacement must suppress geometry submission outside the footprint rather than drawing transparent pixels there.

## When To Use The Clipped Path

Use the clipped path only when all of these are true:

- land overlay manager, not water
- decal overlay, not shadow/ring/heightmap
- a vertex-buffer-extension path is available, or you have your own D3D7 geometry submission path
- at least one texture axis is clamped

If both axes wrap, there is no edge-smear problem to solve. Let vanilla draw it.

If anything looks unsupported or inconsistent, fall back to the original `DrawRect(...)`.

## Core Rendering Algorithm

The clipped renderer should:

1. walk the same visible terrain rows and columns as the vanilla terrain subset draw
2. rebuild the same source terrain triangles
3. compute decal UVs per vertex using the already-selected texture transform
4. clip each triangle against the active decal UV bounds
5. emit only the clipped geometry
6. draw the emitted triangles with the same state already bound by `DrawDecals(...)`

The source geometry remains terrain-following, so the clipped decal works on slopes and across cell diagonals.

The triangles rejected by clipping are simply not submitted in the decal pass. They should reveal the normal terrain that was already drawn earlier in the frame.

## UV Domain

For clamped axes:

- clip against `0 <= u <= 1`
- clip against `0 <= v <= 1`

For wrapped axes:

- do not clip on that axis

This is the critical policy. Only clip on the axes that vanilla treats as non-wrapping.

## Source Terrain Geometry

Reuse the same terrain data used by:

- `cSTETerrainView3D::DrawTerrainMeshSubsetInDrawFrustum(...)`
- `cSTETerrainView3D::DrawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum(...)`

Relevant data from the decomp:

- `sPoints`
- `sAllTriangleIndices`
- `sNumTriangleIndicesPerRow`
- `sDrawColStart`
- `sDrawColEnd`
- `sbDrawTerrainInDrawFrustum`
- `sAllLevelCellIndices`
- `sLevelCellQuads`
- `sLevelCellQuadTriIndices`
- `sVertexCountX`
- `sVertexCountZ`

The clipped renderer should iterate rows and columns exactly like vanilla, then replace the "bulk draw this whole span" step with per-triangle clipping and emission.

On x86, the relevant terrain subset helpers are:

- `DrawTerrainMeshSubsetInDrawFrustum` at `0x007541C0`
- `DrawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum` at `0x007545C0`

Those functions confirm the row/span walk and level-cell-quad pass used by vanilla.

## Per-Vertex Working Format

Use a temporary clip vertex:

```cpp
struct ClipVert {
    float x;
    float y;
    float z;
    uint32_t diffuse;
    float u;
    float v;
};
```

The `u,v` fields are CPU-only for clipping.

The emitted GPU vertex should match the path you choose for final draw submission. If you piggyback the game's D3D7 vertex-buffer-extension path, preserve whatever position/color/texcoord layout that path expects.

## UV Evaluation

Evaluate UVs from the stored decal tex transform:

```cpp
struct Vec4 {
    float x, y, z, w;
};

Vec4 q = Mul(worldToTex4x4, Vec4{pos.x, pos.y, pos.z, 1.0f});

float u = q.x;
float v = q.y;

if (q.w != 0.0f && q.w != 1.0f) {
    u /= q.w;
    v /= q.w;
}
```

Use the same matrix convention that `SC4DrawContext::SetTexTransform4(...)` expects. Verify once against a known decal:

- center of the stamp should land near `(0.5, 0.5)`

If not, swap multiplication order or component interpretation until that holds.

## Triangle Clipping

Clip each triangle with Sutherland-Hodgman.

Potential clip planes:

- `u >= 0`
- `u <= 1`
- `v >= 0`
- `v <= 1`

Apply only the planes for clamped axes.

Intersection for one edge `A -> B` against one clip plane:

```cpp
float t = da / (da - db);
out = Lerp(A, B, t);
```

Interpolate:

- `x, y, z`
- `u, v`
- color if needed

Triangulate the resulting polygon as a fan:

- `(0,1,2)`
- `(0,2,3)`
- ...

## Pseudocode

```cpp
static void EmitClippedTriangle(const SrcVert& s0, const SrcVert& s1, const SrcVert& s2) {
    ClipVert a = MakeClipVert(s0);
    ClipVert b = MakeClipVert(s1);
    ClipVert c = MakeClipVert(s2);

    SmallVector<ClipVert, 8> poly{a, b, c};

    if (clipU) {
        poly = ClipAgainstUMin(poly, 0.0f);
        poly = ClipAgainstUMax(poly, 1.0f);
    }
    if (clipV) {
        poly = ClipAgainstVMin(poly, 0.0f);
        poly = ClipAgainstVMax(poly, 1.0f);
    }

    if (poly.size() < 3) {
        return;
    }

    for (size_t i = 1; i + 1 < poly.size(); ++i) {
        PushGpuVert(poly[0]);
        PushGpuVert(poly[i]);
        PushGpuVert(poly[i + 1]);
    }
}
```

## Geometry Submission On Windows x86

There are two realistic options.

### Option A: Reuse The Game's Vertex-Buffer-Extension Path

Recommended first.

Approach:

- obtain the same vertex-buffer-extension object that `cSTEOverlayManager::DrawOverlays(...)` stores at `this + 0xC0`
- create a dynamic scratch buffer
- fill it with your emitted clipped triangles
- submit it through the same D3D7 render-state and vertex-buffer pipeline the game already uses

Pros:

- least invasive
- matches the game's D3D7 backend expectations
- less state drift

Cons:

- more RE required around the extension's vertex layout

### Option B: Issue Your Own D3D7 Draws

Possible if you already have a stable D3D7 helper path.

Approach:

- build an explicit transformed/lit or untransformed vertex buffer
- submit with a verified `SC4DrawContext::DrawPrims(...)` path or bypass it and draw through raw D3D7

Pros:

- simpler if you already control the vertex layout

Cons:

- easier to break fog, shade, texture stage, or culling assumptions
- you must not blindly trust the vendored draw-service wrapper signatures for this path

Option A is the safer integration for a GZCOM DLL.

### x86 VB-Extension Helper Findings

The current x86 build exposes the scratch-buffer helper cluster used by `DrawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum(...)`:

- `0x0075C9F0`: acquire / begin scratch buffer
- `0x0075CAB0`: configure source index data and orientation flag
- `0x0075CB50`: append one row-span worth of indices into the scratch stream
- `0x0075CAD0`: unlock / flush / draw and reset the scratch state

This is useful, but it also shows a limitation:

- these helpers are tuned for vanilla row spans and implicit index generation
- they are not a drop-in solution for arbitrary per-triangle clipped geometry

So the best expectation is:

- reuse the same low-level vertex-buffer-extension object model
- do not try to force the stock row-span helper functions to emit clipped decals directly

For clipped decals, you should plan to build your own temporary triangle stream and submit it through the same backend style, not through the exact vanilla row-span helper sequence.

## Required Draw-Order Validation

Before implementing full clipping, do a one-decal smoke test in the hook:

1. pick a known decal
2. let `DrawDecals(...)` set up all normal render state for it
3. skip the final `DrawRect(...)` call entirely for that decal

Expected result:

- the terrain under that decal remains visible
- there is no blank or punched-out patch

If skipping `DrawRect(...)` reveals normal terrain, the clipped-geometry design is valid: omitted decal triangles will fall through to the base terrain pass.

If skipping `DrawRect(...)` creates a blank region, stop and re-check the render-order assumption before implementing the full replacement.

## Level-Cell Quad Pass

Vanilla terrain subset drawing also submits level-cell quads separately. Your clipped path must include them or the overlay will have holes/inconsistencies on flattened terrain regions.

For each visible quad:

1. fetch its four vertices
2. split it into two triangles using `sLevelCellQuadTriIndices`
3. clip and emit each triangle like normal terrain triangles

## Cache Design

Do not stop at the uncached version. It will work, but it will be expensive if many decals are active.

Cache one generated clipped mesh per overlay slot.

Suggested key:

- overlay slot index
- overlay rect
- world-to-tex matrix
- overlay flags relevant to clipping
- terrain revision id

Stored value:

- emitted triangle vertex data
- optional index data
- stats such as triangle count

## Cache Invalidation

Invalidate a cached mesh when any of these occur:

- `UpdateDecalInfo`
- `MoveDecal`
- `RemoveOverlay`
- `SetOverlayFlags`
- any terrain redisplay / terrain deformation affecting the rect

If you do not have a cheap terrain revision counter, start with a coarse global invalidation:

- clear the whole cache on terrain edits
- rebuild lazily on next draw

## Interaction With `cSC4PropOccupantTerrainDecal`

No extra persistence work is needed.

Why:

- the occupant stores the same overlay handle and decal parameters
- save/load already rehydrates those parameters
- your hook changes only how decals are rasterized at draw time

So both:

- transient overlays
- persistent terrain-decal props

benefit automatically.

## Versioning

Do not hardcode a single address and call it done.

For Windows builds, keep a version table keyed by known game version signatures. At minimum:

- English 641
- any other Windows executable variants you want to support

The current Ghidra-loaded Windows x86 executable already exposes:

- `DrawRect = 0x00735720`
- `DrawDecals = 0x00736790`

That is enough to prototype the x86 inline hook immediately for this exact binary, but not enough to skip version gating for other builds.

You need:

- `cSTEOverlayManager::DrawDecals` address
- optionally `UpdateDecalInfo`, `MoveDecal`, `RemoveOverlay` if you add cache invalidation hooks
- terrain-view global/static data addresses if they are not reachable from function-local code

If you cannot resolve the version, do not install the patch.

## Failure Policy

This patch should fail soft.

At runtime:

- if the hook cannot be installed, log and continue unpatched
- if the clipped draw path encounters an unsupported case, fall back to vanilla `DrawRect(...)`
- if the cache build fails, fall back to vanilla `DrawRect(...)`

Never leave the engine half-patched.

## Suggested File Layout In A GZCOM DLL

```text
src/dll/hooks/TerrainDecalHook.hpp
src/dll/hooks/TerrainDecalHook.cpp
src/dll/hooks/InlineHook.hpp
src/dll/hooks/InlineHook.cpp
src/dll/terrain/ClippedDecalRenderer.hpp
src/dll/terrain/ClippedDecalRenderer.cpp
src/dll/terrain/DecalClipper.hpp
src/dll/terrain/DecalClipper.cpp
src/dll/terrain/TerrainDecalCache.hpp
src/dll/terrain/TerrainDecalCache.cpp
src/dll/terrain/GameSymbols.hpp
src/dll/terrain/GameSymbols.cpp
```

Responsibilities:

- `InlineHook.*`: 5-byte x86 trampoline install/uninstall
- `GameSymbols.*`: address resolution per SC4 version
- `TerrainDecalHook.*`: hook entry and patch lifecycle
- `DecalClipper.*`: UV clipping math
- `ClippedDecalRenderer.*`: terrain-row walk + geometry emission
- `TerrainDecalCache.*`: cached clipped meshes

## Director Integration

From your director:

1. create a hook controller singleton
2. during `PostAppInit()`:
   - detect SC4 version
   - resolve symbols
   - install the `DrawDecals` hook
3. during shutdown:
   - uninstall hook
   - clear caches

If you already depend on `sc4-render-services`, copy the inline-hook implementation pattern from its sample code directly into your DLL module, but do not treat its `DrawPrims` wrapper signature as authoritative for this renderer.

## Minimal First Milestone

Implement this before adding cache invalidation hooks:

1. patch the `DrawDecals -> DrawRect` call site for the supported x86 build
2. run the one-decal "skip `DrawRect(...)`" smoke test and confirm the base terrain remains visible
3. for supported land decals:
   - build clipped geometry every frame
   - submit it with the verified `DrawPrims(drawContext, primType, vertexFormat, vertexCount, vertexPtr)` calling pattern
4. for everything else:
   - call original draw path

Once that is visually correct, add:

5. per-slot cache
6. invalidation hooks

## Practical Risks

- wrong matrix convention for UV generation
- mismatched vertex layout when using the vertex-buffer-extension path
- missing level-cell quad handling
- using clipping on wrapped axes and breaking intentional tiled decals
- stale cached geometry after terrain edits

The safest way to debug is:

1. start uncached
2. first confirm that skipping `DrawRect(...)` for one decal leaves the base terrain visible
3. render one clearly sub-tile test decal
4. validate flat ground
5. validate slopes
6. validate across flipped terrain diagonals
7. validate level-cell quads
8. only then add caching

## Final Recommendation

For a Windows x86 GZCOM DLL, replace `cSTEOverlayManager::DrawDecals(...)` with an inline trampoline and implement a land-only clipped terrain submission path inside the hook.

That keeps:

- existing save/load
- existing decal placement APIs
- existing render-state setup

while fixing the actual root problem:

- the stock renderer redraws a full terrain rect instead of clipping terrain triangles to the decal UV domain.
