![SC4 Plop and Paint header](docs/images/header.png)

# SC4 Plop and Paint

SC4 Plop and Paint is a SimCity 4 plugin that adds an in-game panel for browsing lots, painting props and flora, and building reusable collections for faster in-game placement.

## What it does

`SC4PlopAndPaint.dll` adds an in-game window called `Advanced Plopping & Painting`. Press `O` in a loaded city to open it. The window is split into five tabs: `Buildings & Lots` for browsing and plopping lots, `Props` for painting individual props, `Prop Families` for managing and painting weighted random prop sets, `Flora` for browsing and painting individual flora, and `Flora Collections` for painting derived flora families and multi-stage chains.

[![Watch the demo on YouTube](docs/images/painting.gif)](https://youtu.be/R7z5wg1KB7E)

_Short demo clip. Click the animation to watch the full video on YouTube._


`_SC4PlopAndPaintCacheBuilder.exe` scans your SimCity 4 plugin directories, parses exemplar and cohort data, and writes `lots.cbor`, `props.cbor`, and `flora.cbor` into your Plugins folder. When thumbnail rendering is enabled, it also writes `lot_thumbnails.bin`, `prop_thumbnails.bin`, and `flora_thumbnails.bin`. The DLL reads those cache files when the city loads, which is why rebuilding the cache matters whenever your plugin collection changes.

## Installation

Install SC4RenderServices first, then download `SC4PlopAndPaint-{version}-Setup.exe` from the releases page and run it.

Dependencies:

- SC4RenderServices (required): GitHub project `https://github.com/caspervg/sc4-render-services`
- SC4RenderServices download page: `https://community.simtropolis.com/files/file/37372-sc4-render-services/`
- Visual C++ 2015-2022 Redistributable (x86, required for SimCity 4 / 32-bit): `https://aka.ms/vs/17/release/vc_redist.x86.exe`
- Visual C++ 2015-2022 Redistributable (x64, required for the cache builder): `https://aka.ms/vs/17/release/vc_redist.x64.exe`
- The bundled cache builder is x64-only and requires 64-bit Windows.

The installer will:

1. Ask for your game root and Plugins directory.
2. Verify that `SC4RenderServices.dll` is already present in your Plugins folder. If it is missing, the installer will stop and direct you to the SC4RenderServices download page.
3. Place `SC4PlopAndPaint.dll` and `SC4PlopAndPaint.dat` in your Plugins folder.
4. Let you choose the thumbnail size used for cache generation and apply the same size to `ThumbnailDisplaySize` in `SC4PlopAndPaint.ini`.
5. Place `_SC4PlopAndPaintCacheBuilder.exe` and a generated `Rebuild-Cache.cmd` in `Documents\SimCity 4\SC4PlopAndPaint\`.
6. Optionally run the cache builder immediately.

To rebuild the cache later, for example after adding or removing plugins, run `Rebuild-Cache.cmd`.

If something looks wrong in game, check the separate services plugin's log output in `Documents\SimCity 4\`.

## Using it in-game

For the full player guide, including tab-by-tab controls, paint and strip hotkeys, screenshots, and example use cases, see [docs/USAGE.md](docs/USAGE.md).

Quick summary:

- `Buildings & Lots` is for browsing and plopping lots, including growables you want to place manually
- `Props` is for browsing props, painting a single prop, and removing placed props with strip mode
- `Prop Families` is for building weighted prop palettes and painting with them in direct, line, or polygon mode
- `Flora` is for browsing flora exemplars, painting one flora type, and favoriting individual flora
- `Flora Collections` is for painting flora families and multi-stage chains derived from the cache
- Paint mode supports grid controls, snapping, undo, line placement, polygon fills, and a live in-world status window

If you just want the basics:

1. Load a city and open the panel with the toggle shortcut (packaged default: `O`)
2. Use `Paint` from `Props` or `Flora`, `Paint family` from `Prop Families`, or `Paint` from `Flora Collections`
3. Choose a mode and options, then press `Start`
4. Use `Enter` to generate and commit placements, `Ctrl+Z` / `Ctrl+Backspace` to undo, and `Esc` to cancel

## Configuration

`dist/SC4PlopAndPaint.ini` contains the main runtime defaults for the DLL.

Notable options:

- `LogLevel` and `LogToFile` control logging verbosity and file logging
- `EnableDrawOverlay` disables or enables the draw-service overlay preview used while painting
- `DefaultPropPreviewMode`, `DefaultShowGridOverlay`, `DefaultSnapPointsToGrid`, `DefaultSnapPlacementsToGrid`, and `DefaultGridStepMeters` control the default paint popup values
- `ThumbnailDisplaySize` controls thumbnail size in the UI
- `ThumbnailBackgroundColor` and `ThumbnailBorderColor` control the thumbnail slot styling used behind transparent thumbnails

Color options accept `RRGGBB` or `RRGGBBAA`. Setting either thumbnail color option to an empty value makes it transparent.

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
| [Fira Mono](https://fonts.google.com/specimen/Fira+Mono) | Embedded monospace font | SIL Open Font License 1.1 |

Full license texts are in [dist/ThirdPartyNotices.txt](dist/ThirdPartyNotices.txt).
