# SC4 Plop and Paint - Linux cache builder

SimCity 4 and the SC4PlopAndPaint.dll plugin run under Wine or Steam Proton. This archive contains a native Linux
build of the cache builder, so building the plugin's cache (lots.cbor, props.cbor, flora.cbor and the thumbnail .bin
files in your Plugins folder) does not need Wine.

Contents:
- SC4PlopAndPaintCacheBuilder: the cache builder
- rebuild-cache.sh: runs the cache builder and writes cache_build.log
- PropertyMapper.xml: property definitions, keep it next to the cache builder

Requirements:
- x86-64 Linux with glibc 2.28 or newer.
- For 3D thumbnail rendering: a desktop session with OpenGL 3.3. Without one the cache is still built, just
  without rendered thumbnails.

Installing the plugin:
- Install SC4RenderServices and SC4 Plop and Paint into the Plugins folder of the Wine prefix SimCity 4 runs in,
  for example by running both Windows installers in that prefix:
    WINEPREFIX=/path/to/prefix wine SC4PlopAndPaint-<version>-Setup.exe
  For Steam, use protontricks-launch with the SimCity 4 app id instead.
- In the installer's Plugins step, browse to the prefix's drive_c/users/<user>/Documents/SimCity 4/Plugins folder.
- You can untick "Build cache during installation" and use this cache builder instead.

Building the cache:
- Extract this archive anywhere, then run ./rebuild-cache.sh after adding, removing or changing plugins.
- Run ./SC4PlopAndPaintCacheBuilder without arguments to print the folders it detected.
- The game and Plugins folders are detected from, in order: --wine-prefix, $WINEPREFIX, ~/.wine and Steam Proton
  prefixes. The game folder is also looked up in Steam libraries. Edit the variables at the top of rebuild-cache.sh,
  or pass the options below, if the wrong folders are picked.

Command-line options:
  --scan                   Build the cache (without it, only the detected folders are printed)
  --game <dir>             SimCity 4 game folder, the one that contains Apps
  --plugins <dir>          Your Documents/SimCity 4/Plugins folder, where the cache is written
  --wine-prefix <dir>      Wine prefix to detect --game and --plugins from
  --locale <name>          Locale folder under the game folder (default: English)
  --thumbnail-size <px>    Thumbnail size, 22-176 (default: 44); match ThumbnailDisplaySize in SC4PlopAndPaint.ini
  --render-thumbnails      Render 3D thumbnails for items without an icon (slower)

Examples:
  ./SC4PlopAndPaintCacheBuilder --scan --wine-prefix ~/Games/simcity-4
  ./SC4PlopAndPaintCacheBuilder --scan --game "/path/to/SimCity 4 Deluxe Edition" \
      --plugins "/path/to/prefix/drive_c/users/$USER/Documents/SimCity 4/Plugins"
