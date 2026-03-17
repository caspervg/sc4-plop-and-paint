# SC4 Plop and Paint

This release zip includes:
- the Windows installer
- LICENSE.txt
- THIRD_PARTY_NOTICES.txt

Installation instructions:
- Install SC4RenderServices first so SC4RenderServices.dll is already present in your Plugins folder.
- SC4RenderServices project page: https://github.com/caspervg/sc4-render-services
- SC4RenderServices download page: https://community.simtropolis.com/files/file/37372-sc4-render-services/
- Install the Microsoft Visual C++ 2015-2022 Redistributables:
- x86 (required for SimCity 4 / 32-bit): https://aka.ms/vs/17/release/vc_redist.x86.exe
- x64 (required for the cache builder): https://aka.ms/vs/17/release/vc_redist.x64.exe
- The bundled cache builder is x64-only and requires 64-bit Windows.
- Run the included SC4 Plop and Paint installer.
- The installer will ask for your SimCity 4 game root and Plugins folder, verify the SC4RenderServices dependency, and install the plugin files. If SC4RenderServices is missing, the installer will stop and tell you where to download it.
- The installer also lets you choose the thumbnail size used for cache generation and sets the same size in SC4PlopAndPaint.ini for the in-game UI.
- The installer can also run the cache builder for you. If you skip that step, run Rebuild-Cache.cmd later from Documents\SimCity 4\SC4PlopAndPaint\.
- The cache builder scans your game and Plugins folders and writes lots.cbor, props.cbor and flora.cbor into your Plugins folder.
- If thumbnail rendering is enabled, it also writes lot_thumbnails.bin, prop_thumbnails.bin and flora_thumbnails.bin into your Plugins folder.

Usage instructions:
- After installation, press O in game to open the lot plop and prop paint dialog.
- If something looks off, check <My Documents>/SimCity 4/ for the separate services plugin log output.
