@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
set "EXE=%SCRIPT_DIR%_SC4PlopAndPaintCacheBuilder.exe"

if not exist "%EXE%" (
  echo Could not find "%EXE%"
  echo Build the CLI first or update this path in RUN_CACHE_BUILDER.cmd.
  pause
  exit /b 1
)

if /I "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 (
  echo This cache builder requires 64-bit Windows.
  pause
  exit /b 1
)

echo === SC4 Plop and Paint Cache Builder ===
echo.

set "DO_SCAN="
set /p "DO_SCAN=Run cache building scan now? (y/N): "
if /I not "%DO_SCAN%"=="Y" (
  echo.
  echo Skipping scan.
  pause
  exit /b 0
)

echo.
set "DEFAULT_GAME_ROOT=%ProgramFiles(x86)%\SimCity 4 Deluxe Edition"
if not defined ProgramFiles(x86) set "DEFAULT_GAME_ROOT=%ProgramFiles%\SimCity 4 Deluxe Edition"
set "DEFAULT_PLUGINS=%USERPROFILE%\Documents\SimCity 4\Plugins"
set "DEFAULT_LOCALE=English"
set "DEFAULT_THUMBNAIL_SIZE=44"

set "GAME_ROOT="
set "PLUGINS_DIR="
set "LOCALE_DIR="
set "RENDER_THUMBS="
set "THUMBNAIL_SIZE="

set /p "GAME_ROOT=Game root directory [default: %DEFAULT_GAME_ROOT%]: "
if not defined GAME_ROOT set "GAME_ROOT=%DEFAULT_GAME_ROOT%"

set /p "PLUGINS_DIR=User Plugins directory [default: %DEFAULT_PLUGINS%]: "
if not defined PLUGINS_DIR set "PLUGINS_DIR=%DEFAULT_PLUGINS%"

set /p "LOCALE_DIR=Locale under game root [default: %DEFAULT_LOCALE%]: "
if not defined LOCALE_DIR set "LOCALE_DIR=%DEFAULT_LOCALE%"

set /p "RENDER_THUMBS=Render 3D thumbnails? (y/N): "

set /p "THUMBNAIL_SIZE=Thumbnail size in pixels (22-176) [default: %DEFAULT_THUMBNAIL_SIZE%]: "
if not defined THUMBNAIL_SIZE set "THUMBNAIL_SIZE=%DEFAULT_THUMBNAIL_SIZE%"

echo.
echo Running:
echo "%EXE%" --scan --game "%GAME_ROOT%" --plugins "%PLUGINS_DIR%" --locale "%LOCALE_DIR%" --thumbnail-size "%THUMBNAIL_SIZE%"
if /I "%RENDER_THUMBS%"=="Y" echo   --render-thumbnails

if /I "%RENDER_THUMBS%"=="Y" (
  "%EXE%" --scan --game "%GAME_ROOT%" --plugins "%PLUGINS_DIR%" --locale "%LOCALE_DIR%" --thumbnail-size "%THUMBNAIL_SIZE%" --render-thumbnails
) else (
  "%EXE%" --scan --game "%GAME_ROOT%" --plugins "%PLUGINS_DIR%" --locale "%LOCALE_DIR%" --thumbnail-size "%THUMBNAIL_SIZE%"
)

echo.
pause
