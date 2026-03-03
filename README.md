# SC4 Plop and Paint

SC4 Plop and Paint is a SimCity 4 plugin that adds an in-game panel for browsing lots, painting props, and building reusable weighted prop families.

## What it does

`SC4PlopAndPaint.dll` adds an in-game window called `Advanced Plopping & Painting`. Press `O` in a loaded city to open it. The window is split into three tabs: `Buildings & Lots` for browsing and plopping lots, `Props` for painting individual props, and `Families` for managing and painting weighted random prop sets.

`_SC4PlopAndPaintCacheBuilder.exe` scans your SimCity 4 plugin directories, parses exemplar and cohort data, and writes `lot_configs.cbor` and `props.cbor` into your Plugins folder. The DLL reads those cache files when the city loads, which is why rebuilding the cache matters whenever your plugin collection changes.

## Installation

Install SC4 Render Services first, then download `SC4PlopAndPaint-{version}-Setup.exe` from the releases page and run it. The installer will:

1. Ask for your game root and Plugins directory.
2. Verify that `SC4RenderServices.dll` is already present in your Plugins folder.
3. Place `SC4PlopAndPaint.dll` and `SC4PlopAndPaint.dat` in your Plugins folder.
4. Place `_SC4PlopAndPaintCacheBuilder.exe` and a generated `Rebuild-Cache.ps1` in `Documents\SimCity 4\SC4PlopAndPaint\`.
5. Optionally run the cache builder immediately.

Required runtimes:

- Visual C++ 2015-2022 Redistributable (x86, required for SimCity 4 / 32-bit): `https://aka.ms/vs/17/release/vc_redist.x86.exe`
- Visual C++ 2015-2022 Redistributable (x64): `https://aka.ms/vs/17/release/vc_redist.x64.exe`

To rebuild the cache later, for example after adding or removing plugins, run `Rebuild-Cache.ps1`.

If something looks wrong in game, check the separate services plugin's log output in `Documents\SimCity 4\`.

## Using it in-game

This section is written for players, not developers. It covers the full in-game flow after installation and after the cache has been built.

### Before you start

1. Run the cache builder if you have added, removed, or changed plugins.
2. Start SimCity 4 and load a city.
3. Press the panel toggle shortcut to open the `Advanced Plopping & Painting` window. The packaged default is `O`.

If the panel opens but the lists are empty, `Buildings & Lots` is missing `lot_configs.cbor`, or `Props` / `Families` are missing `props.cbor`.

The main window can be closed either with the same toggle shortcut or with the window close button.

### The main window

The window has three tabs:

- `Buildings & Lots`: browse buildings and plop lots
- `Props`: browse props, paint a single prop, and remove placed props
- `Families`: manage built-in and custom prop families, then paint with a weighted random palette

### Buildings & Lots

This tab is for finding and plopping lots.

Filters:

- `Search buildings...`: matches both building names and lot names
- Zone: `Any zone`, `Residential (R)`, `Commercial (C)`, `Industrial (I)`, `Plopped`, `None`, `Other`
- Wealth: `Any wealth`, `$`, `$$`, `$$$`
- Growth stage: `Any stage`, `Plopped (255)`, `0` through `15`
- `Favorites only`
- `Width` and `Depth` min/max sliders
- `Occupant Groups`: expandable tree filter with `Clear OGs`
- `Clear filters`: resets all of the above

Important behavior:

- Filtering is lot-based, but the list shows buildings. A building stays visible if any of its lots matches the current filters.
- The upper table lists buildings. Single-click selects one.
- Double-clicking a building row auto-plops only when that building has exactly one lot.
- The lower table shows the selected building's lots.

Lot actions:

- Double-click a lot row to plop it
- Press `Plop` to plop it
- Use `Star` / `Unstar` to manage lot favorites

### Props

This tab is for individual props and prop removal.

Filters:

- `Search props...`
- `Favorites only`
- Width, height, and depth min/max sliders
- `Clear filters`

Per-prop actions:

- `Paint`: start painting that prop, or switch the active paint tool to that prop if you are already painting
- `Star` / `Unstar`: manage prop favorites
- `Fam`: add the prop to one of your custom families

If you hover a prop name and it belongs to built-in families, the tooltip shows those family IDs and names.

Strip mode:

- `Strip props`: enters prop removal mode
- `Stop stripping`: exits strip mode from the tab
- In strip mode, click props in the city to delete them one by one
- `Ctrl+Z` restores the most recently removed prop
- `Esc` or right-click exits strip mode

### Families

This tab combines built-in families from the cache with your own custom families.

Filters and top actions:

- `Search family name...`
- `Filter IID (0x...)`
- `Clear filters`
- `New family`
- `Stop painting` while paint mode is active

Family list:

- `Built-in` families are read-only and show their instance IDs
- `Own` families are your editable custom families
- Double-click a family row or press `Paint` to open the family paint options

Custom family management:

- Create a family with `New family`
- Delete the selected custom family with `Delete family`
- Add props from the `Props` tab using `Fam`
- Adjust entry `Weight` values to bias random selection
- Remove entries with the `x` button

Built-in families cannot be edited.

### Favorites

Favorites are there to reduce repeated searching.

In `Buildings & Lots`, press `Star` on a lot to save it and `Unstar` to remove it. Turning on `Favorites only` limits the visible list to your saved lot favorites that still match the other filters.

In `Props`, `Star`, `Unstar`, and `Favorites only` work the same way for props.

Favorites are stored by the plugin and persist between sessions.

### Creating and managing your own families

Custom families are your own weighted random prop palettes.

To create one, open `Families`, press `New family`, enter a name, and press `Create`. The new family starts empty.

The fastest way to add props to a family is from the `Props` tab: find a prop, press `Fam`, and choose the destination family from the popup. Once a family has members, return to `Families` to review and edit it.

When a custom family is selected, you can delete it, change each entry's weight, and remove individual entries with the `x` button. Higher weights make an entry more likely to be chosen during family painting.

Built-in families (i.e. provided by Maxis or custom content creators) are read-only. Custom families are editable.

### Starting paint mode

Painting can be started from the `Props` tab for a single fixed prop or from the `Families` tab for a weighted random family. Before the tool activates, a popup lets you choose the paint settings.

Common options:

- `Mode`: `Direct paint`, `Paint along line`, or `Paint inside polygon`
- `Rotation`: fixed 0 / 90 / 180 / 270 degrees
- `Show grid overlay`: shows the preview grid on the terrain
- `Snap points to grid`: snaps the points you click to the grid
- `Also snap placements to grid`: snaps final placed props to the grid as well
- `Grid step (m)`: grid size in meters
- `Vertical offset (m)`: raises placed props and the preview above the terrain
- `Direct preview`: `Outline only`, `Full prop only`, or `Outline + full prop`

Mode-specific options:

- Line mode: `Spacing (m)`, `Align to path direction`, `Random rotation`, and `Lateral jitter (m)`
- Polygon mode: `Density (/100 m^2)`, `Density variation`, and `Random rotation`

Polygon notes:

- `Density` currently ranges from `0.1` to `10.0` per `100 m^2`
- `Density variation` ranges from `0.0` to `1.0`
- `Density variation` controls how uniform or patchy the fill looks: `0` is more even, `1` creates more clusters and gaps

When painting with a family, each placed prop is chosen from that family using its saved weights.

### How the paint tool works

After you press `Start`, the paint tool takes over map input until you commit or cancel.

In `Direct paint`, left-click places one prop at a time.

In `Paint along line`, left-click adds line points and `Enter` places props along the path.

In `Paint inside polygon`, left-click adds polygon vertices and `Enter` fills the area.

In line and polygon modes, the first `Enter` generates the batch and places it as pending props. Press `Enter` again to commit those pending props permanently.

### Paint tool controls

While paint mode is active:

- `R`: rotate the fixed rotation (cycles 0 / 90 / 180 / 270)
- `G`: toggle the grid overlay
- `S`: toggle point snapping (turning it off also disables placement snapping)
- `[` / `]`: step the grid size through preset values (`1`, `2`, `4`, `8`, `16` meters)
- `P`: cycle preview mode (`Outline`, `Full`, `Combined`, `Hidden`)
- `Backspace`: remove the last unconfirmed line point or polygon vertex while drawing
- `Ctrl+Z`: undo the whole last pending placement group
- `Ctrl+Backspace`: undo only the last prop in the current top group
- `Enter`: place the current line or polygon batch, or commit pending placements
- `Esc` or right-click: cancel all pending placements and leave paint mode

Mode-specific hotkeys:

- Line mode: `-` / `+` decrease or increase spacing by `0.5m`
- Polygon mode: `-` / `+` decrease or increase density by `0.25`
- Polygon mode: `Ctrl+-` / `Ctrl++` decrease or increase density variation by `0.05`

While painting, a small status window also shows:

- The current mode
- Grid state and grid size
- Snap state
- Preview mode
- Current line spacing or polygon density
- Current polygon density variation

That status window also mirrors the main paint hotkeys for quick reference.

### What "pending placements" means

Painted props are first placed in a temporary highlighted state. They are visible immediately, but they are still considered pending until you commit them. While they are pending, you can undo them. Press `Enter` to commit everything currently pending, or press `Esc` to remove all pending placements instead.

That means even in direct mode there are usually two stages: you click to place one or more props, then you press `Enter` when you are satisfied and want to make them final.

### Typical flows

**Plop a lot**

1. Press the panel toggle shortcut (default `O`).
2. Open `Buildings & Lots`.
3. Filter until you find the building or lot you want.
4. Double-click the lot row or press `Plop`.

**Paint one prop repeatedly**

1. Press the panel toggle shortcut (default `O`).
2. Open `Props`.
3. Find the prop and press `Paint`.
4. Choose `Direct paint` or another mode and set the options you want.
5. Press `Start`.
6. Click to place props.
7. Use `Ctrl+Z` or `Ctrl+Backspace` if needed.
8. Press `Enter` to commit, or `Esc` to cancel.

**Paint a row of props**

1. Start from `Props` or `Families`.
2. Choose `Paint along line`.
3. Press `Start`.
4. Click each control point along the line.
5. Press `Enter` to generate the placements.
6. Press `Enter` again to commit them, or undo or cancel instead.

**Paint a filled area**

1. Start from `Props` or `Families`.
2. Choose `Paint inside polygon`.
3. Set `Density`, `Density variation`, and any other options you want.
4. Press `Start`.
5. Click each polygon vertex.
6. Press `Enter` to fill the area.
7. Press `Enter` again to commit, or use undo if needed.

**Paint a random family**

1. Open `Families`.
2. Select or create a family.
3. Adjust the family weights if needed.
4. Press `Paint family`.
5. Choose a mode and options.
6. Paint as normal; each placement will be chosen from the family.

### Example use cases

**Replace repeated manual lot searching**

If you keep using the same transit, utility, landmark, or decoration lots:

1. Find them once in `Buildings & Lots`.
2. Mark them with `Star`.
3. Turn on `Favorites only` later when you want a short working list.

This is useful when your Plugins folder is large and you want a compact set of go-to lots.

**Plop lots that are normally only seen as growables**

The lot browser is useful even when you are not sure how a building is normally obtained in gameplay:

1. Open `Buildings & Lots`.
2. Filter by zone, wealth, growth stage, size, or occupant groups.
3. Find a lot that would normally only appear as a growable.
4. Double-click it or press `Plop` to place it directly.

This makes it easier to use growable-content lots intentionally in custom cities, test setups, or showcase regions.

**Discover content you forgot you installed**

The browsing tabs can also be used as a discovery tool:

1. Open `Buildings & Lots`, `Props`, or `Families`.
2. Search loosely by theme, size, or category instead of by exact name.
3. Browse thumbnails, family tooltips, and filtered lists.
4. Use `Star` to save anything you want to come back to later.

This is useful when you have a large Plugins collection and want to rediscover buildings, lots, or props you did not remember having.

**Lay out fences, hedges, or roadside details quickly**

Use `Paint along line` when you want a clean repeated sequence along a street, path, or lot edge:

1. Start from `Props` or `Families`.
2. Choose `Paint along line`.
3. Set `Spacing`, `Align to path direction`, and optional `Lateral jitter`.
4. Click the control points along the route.
5. Press `Enter` to generate the row, then `Enter` again to commit.

This works well for fences, bollards, lamps, benches, and other evenly spaced props.

**Place road signs, highway signs, or sound barriers along a route**

Line paint is also useful for transportation detailing where orientation and spacing matter:

1. Start from `Props` for one repeated asset, or `Families` for a small themed set.
2. Choose `Paint along line`.
3. Turn on `Align to path direction`.
4. Adjust `Spacing` until the preview matches the rhythm you want.
5. Click along the road, ramp, or highway edge.
6. Press `Enter` to generate the run, then `Enter` again to commit.

This is a good fit for roadside signs, gantry-adjacent details, retaining props, and repeated sound barriers.

**Fill parks, medians, or vacant corners with less repetitive clutter**

Use `Paint inside polygon` when you want to cover an area instead of drawing a single line:

1. Start from `Props` for one repeated prop, or `Families` for a mixed palette.
2. Set `Density` for overall coverage.
3. Set `Density variation` higher if you want more natural clumps and gaps.
4. Click the polygon outline.
5. Press `Enter` to generate the fill, then `Enter` again to commit.

This is the fastest way to block in planters, shrubs, rocks, debris, or plaza furniture across an irregular shape.

**Dress up industrial or port areas with mixed clutter**

Custom families and polygon fill work well for cargo-heavy scenes:

1. Create a family for containers, pallets, crates, drums, or similar props.
2. Add matching entries from the `Props` tab with `Fam`.
3. Raise the `Weight` of the props you want to dominate the mix.
4. Start painting from `Families` with `Paint inside polygon`.
5. Tune `Density` and `Density variation` until the preview looks believable.
6. Fill warehouse yards, loading areas, docks, or fenced industrial corners.

This is an efficient way to create busy-looking industry and port spaces without hand-placing every single object.

**Build a reusable random palette for one theme**

If you often place the same style of clutter together, create a custom family:

1. Create a family in `Families`.
2. Add matching props from the `Props` tab with `Fam`.
3. Increase the `Weight` of the props you want to appear more often.
4. Paint with that family in direct, line, or polygon mode.

This is useful for themed sets like street furniture, industrial clutter, plaza details, or shoreline props.

**Remove mistakes without restarting the whole pass**

When painting or stripping, you do not need to stop and start over for small corrections:

1. While painting, use `Ctrl+Z` to remove the most recent group, or `Ctrl+Backspace` to trim the last prop from that group.
2. While stripping, use `Ctrl+Z` to restore the most recently removed prop.
3. Use `Esc` if you want to abandon the current paint batch or leave strip mode.

This makes it practical to work iteratively instead of treating each pass as all-or-nothing.

### Stopping paint mode

Press `Esc` to cancel pending placements and leave paint mode immediately. If you reopen the panel while painting, the `Props` and `Families` tabs also show a `Stop painting` button.

## Building from source

Clone with submodules:

```bash
git clone --recurse-submodules https://github.com/caspervg/sc4-advanced-plop
cd sc4-advanced-plop
```

**DLL (32-bit, Windows only - required for SC4):**

```bash
cmake --preset vs2022-win32-release
cmake --build --preset vs2022-win32-release-build --target SC4PlopAndPaint
```

**Cache builder CLI (64-bit, Windows):**

```bash
cmake --preset vs2022-x64-release
cmake --build --preset vs2022-x64-release-build --target SC4PlopAndPaintCli
```

**macOS / Linux (CLI only):**

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release-build
```

Use `ninja-debug`, `vs2022-win32-debug`, or `vs2022-x64-debug` for debug builds.

Dependencies are managed via vcpkg (bundled in `vendor/vcpkg`). On Windows, the CI workflow also builds `sc4-imgui-service` separately before packaging the release artifact.

**Running tests:**

```bash
ctest -C Debug --test-dir cmake-build-debug-visual-studio --output-on-failure
```

## Code overview

```text
src/
|- shared/          # Header-only core library (SC4PlopAndPaintCore)
|  |- entities.hpp     # Data structs: Building, Lot, Prop, FamilyEntry, favorites, etc.
|  `- tests/           # Catch2 unit tests
|- app/             # Cache builder CLI (SC4PlopAndPaintCli)
|  `- main.cpp         # Arg parsing, plugin scanning, CBOR export
`- dll/             # In-game plugin - Windows 32-bit only
   |- SC4PlopAndPaintDirector.*   # GZCOM director: lifecycle, data loading, favorites
   |- LotPlopPanel.*              # Top-level ImGui panel and tab management
   |- BuildingsPanelTab.*         # Lot browser tab
   |- PropPanelTab.*              # Prop browser tab
   |- FamiliesPanelTab.*          # Family browser and editor tab
   |- PropPainterInputControl.*   # Mouse/keyboard input for prop painting
   |- PropLinePlacer.*            # Line-mode prop placement
   `- PropPolygonPlacer.*         # Polygon-fill prop placement
vendor/
|- DBPFKit/            # SC4 DBPF file format parser
|- gzcom-dll/          # GZCOM interface headers
|- reflect-cpp/        # Reflection/serialization (CBOR, JSON)
|- sc4-imgui-service/  # ImGui integration for SC4
`- vcpkg/              # Package manager
```

### Key design points

- **Shared entities** (`src/shared/entities.hpp`) are plain structs annotated with `rfl::Hex<T>` and `rfl::TaggedUnion` for automatic CBOR serialization via reflect-cpp.
- **Cache builder scanning** is two-pass: buildings are parsed first, then lot configs are resolved against the building map, including family-based growable lot references. Output is written as CBOR binary files.
- **DLL** loads the CBOR files on city init (`PostCityInit`), then renders the ImGui panel each frame. Prop painting uses a custom GZCOM input control and a draw service overlay for on-map feedback.

## Third-party code

| Library | Purpose | License |
|---|---|---|
| [sc4-render-services](https://github.com/caspervg/sc4-render-services) | ImGui backend and SC4 custom services integration | LGPL 2.1 |
| [gzcom-dll](https://github.com/nsgomez/gzcom-dll) | GZCOM interface headers for SC4 plugin development | LGPL 2.1 |
| [DBPFKit](https://github.com/caspervg/DBPFKit) | DBPF archive reader (exemplars, FSH, S3D, LText) | - |
| [Dear ImGui](https://github.com/ocornut/imgui) | Immediate-mode UI framework | MIT |
| [reflect-cpp](https://github.com/getml/reflect-cpp) | Compile-time reflection and CBOR/JSON serialization | MIT |
| [spdlog](https://github.com/gabime/spdlog) | Structured logging | MIT |
| [args](https://github.com/Taywee/args) | CLI argument parsing | MIT |
| [pugixml](https://github.com/zeux/pugixml) | XML parsing (PropertyMapper) | MIT |
| [yyjson](https://github.com/ibireme/yyjson) | Fast JSON parsing (reflect-cpp backend) | MIT |
| [stb](https://github.com/nothings/stb) | Image decoding/encoding | MIT / Public Domain |
| [WIL](https://github.com/microsoft/wil) | Windows Implementation Library helpers | MIT |
| [libsquish](https://sourceforge.net/projects/libsquish/) | DXT texture decompression | MIT |
| [mio](https://github.com/mandreyel/mio) | Memory-mapped file I/O | MIT |
| [jsoncons](https://github.com/danielaparker/jsoncons) | JSON/CBOR processing | Boost 1.0 |
| [utfcpp](https://github.com/nemtrif/utfcpp) | UTF-8 string utilities | Boost 1.0 |
| [ctre](https://github.com/hanickadot/compile-time-regular-expressions) | Compile-time regular expressions | Apache 2.0 |
| [raylib](https://www.raylib.com) | 3D rendering for thumbnail generation | zlib |
| [GLFW](https://www.glfw.org) | OpenGL windowing (raylib dependency) | zlib |

Full license texts are in [dist/ThirdPartyNotices.txt](dist/ThirdPartyNotices.txt).
