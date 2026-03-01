# SC4 Plop and Paint

This release zip includes:
- the Windows installer
- LICENSE.txt
- THIRD_PARTY_NOTICES.txt

Installation instructions:
- Install SC4 Render Services first so SC4RenderServices.dll is already present in your Plugins folder.
- Install the Microsoft Visual C++ 2015-2022 Redistributables:
- x86 (required for SimCity 4 / 32-bit): https://aka.ms/vs/17/release/vc_redist.x86.exe
- x64 (also installed): https://aka.ms/vs/17/release/vc_redist.x64.exe
- Run the included SC4 Plop and Paint installer.
- The installer will ask for your SimCity 4 game root and Plugins folder, verify the SC4 Render Services dependency, and install the plugin files.
- The installer can also run the cache builder for you. If you skip that step, run Rebuild-Cache.ps1 later from Documents\SimCity 4\SC4PlopAndPaint\.
- The cache builder scans your game and Plugins folders and writes lot_configs.cbor and props.cbor into your Plugins folder.

Usage instructions:
- After installation, press O in game to open the lot plop and prop paint dialog.
- If something looks off, check <My Documents>/SimCity 4/ for the separate services plugin log output.
