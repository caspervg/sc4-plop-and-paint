# SC4 Plop and Paint

Installation instructions:
- Move the files in PLACE_IN_YOUR_PLUGINS_FOLDER to your <My Documents>/SimCity 4/Plugins directory
- Generate the cache files by right clicking on RUN_CACHE_BUILDER.ps1 and selecting "Run with PowerShell". This index your SC4 files and plugins and generate the thumbnail icons for lots/buildings and props.
- The cache building script will guide you where necessary. It may take a while for large Plugins folders
- At the end, two new files will appear in your Plugins directory, "lot_configs.cbor" and "props.cbor". These are the serialized caches that the plop and paint DLL will use to quickly load the list of lots/buildings and props.

Usage instructions:
- After the installation, press O in game to open the lot plop and prop paint dialog.
- If something looks off, check <My Documents>/SimCity 4/ for the separate services plugin log output
