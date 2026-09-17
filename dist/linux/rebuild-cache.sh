#!/usr/bin/env bash
# Rebuilds the SC4 Plop and Paint cache (lots.cbor, props.cbor, flora.cbor) in your SimCity 4 Plugins folder.
#
# Folders are detected automatically from your Wine prefix ($WINEPREFIX, ~/.wine) or Steam Proton prefix.
# Run ./SC4PlopAndPaintCacheBuilder without arguments to see what gets detected, and set the variables
# below if it picks the wrong folders.

GAME_DIR=""           # SimCity 4 game folder, the one that contains Apps
PLUGINS_DIR=""        # Your Documents/SimCity 4/Plugins folder, where the cache is written
WINE_PREFIX=""        # Wine prefix SimCity 4 runs in, used to detect the two folders above
LOCALE="English"      # Locale folder under the game folder
THUMBNAIL_SIZE=44     # 22-176; keep in sync with ThumbnailDisplaySize in SC4PlopAndPaint.ini
RENDER_THUMBNAILS=0   # 1 renders 3D thumbnails for items without an icon (slower, needs a desktop session)

set -uo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cli_args=(--scan --locale "$LOCALE" --thumbnail-size "$THUMBNAIL_SIZE")
if [ -n "$GAME_DIR" ]; then cli_args+=(--game "$GAME_DIR"); fi
if [ -n "$PLUGINS_DIR" ]; then cli_args+=(--plugins "$PLUGINS_DIR"); fi
if [ -n "$WINE_PREFIX" ]; then cli_args+=(--wine-prefix "$WINE_PREFIX"); fi
if [ "$RENDER_THUMBNAILS" = "1" ]; then cli_args+=(--render-thumbnails); fi

log_file="$script_dir/cache_build.log"
echo "Writing cache build log to $log_file"
"$script_dir/SC4PlopAndPaintCacheBuilder" "${cli_args[@]}" 2>&1 | tee "$log_file"
exit_code=$?

if [ "$exit_code" -eq 0 ]; then
    echo "Cache build completed successfully."
else
    echo "Cache build failed with exit code $exit_code. See $log_file for details."
fi
exit "$exit_code"
