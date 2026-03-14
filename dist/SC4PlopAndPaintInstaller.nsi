!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "nsDialogs.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!cd "${__FILEDIR__}"

!define APP_NAME "SC4 Plop and Paint"
!ifndef APP_VERSION
  !define APP_VERSION "dev"
!endif
!define APP_TOOLS_SUBDIR "SC4PlopAndPaint"
!define RENDER_SERVICES_DLL_NAME "SC4RenderServices.dll"
!ifndef MIN_RENDER_SERVICES_VERSION_MAJOR
  !define MIN_RENDER_SERVICES_VERSION_MAJOR 0
!endif
!ifndef MIN_RENDER_SERVICES_VERSION_MINOR
  !define MIN_RENDER_SERVICES_VERSION_MINOR 0
!endif
!ifndef MIN_RENDER_SERVICES_VERSION_BUILD
  !define MIN_RENDER_SERVICES_VERSION_BUILD 1
!endif
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\SC4PlopAndPaint"
!define APP_REG_KEY "Software\SC4PlopAndPaint"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "SC4PlopAndPaint-${APP_VERSION}-Setup.exe"
Unicode True
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show

Var Dialog
Var GameRoot
Var SC4PluginsDir
Var SC4ToolsDir
Var HGameRoot
Var HPluginsDir
Var HBrowseGameRoot
Var HBrowsePluginsDir
Var CacheLocale
Var CacheRenderThumbs
Var CacheBuildNow
Var CacheThumbnailSize
Var HCacheLocale
Var HCacheRenderThumbs
Var HCacheBuildNow
Var HCacheThumbnailSize
Var HSummaryText
Var GameExePath
Var RenderServicesDllPath

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Open SC4 Plop and Paint output folder"
!define MUI_FINISHPAGE_RUN_FUNCTION OpenToolsFolder
!define MUI_FINISHPAGE_RUN_CHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "THIRD_PARTY_NOTICES.txt"
Page Custom ConfigurePathsPage ConfigurePathsPageLeave
Page Custom ConfigureCachePage ConfigureCachePageLeave
Page Custom ConfigureSummaryPage
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Function .onInit
  SetShellVarContext current
  Call DetectDefaultGameRoot
  StrCpy $SC4PluginsDir "$DOCUMENTS\SimCity 4\Plugins"
  StrCpy $CacheLocale "English"
  StrCpy $CacheRenderThumbs "0"
  StrCpy $CacheBuildNow "1"
  StrCpy $CacheThumbnailSize "44"
FunctionEnd

Function DetectDefaultGameRoot
  ; SC4 is a 32-bit app, so read from the 32-bit registry view.
  StrCpy $GameRoot "$PROGRAMFILES32\SimCity 4 Deluxe Edition"
  SetRegView 32
  ReadRegStr $0 HKLM "SOFTWARE\Maxis\SimCity 4" "Install Dir"
  ${If} $0 != ""
    StrCpy $GameRoot $0
  ${EndIf}
FunctionEnd

Function ValidateGameExecutable
  StrCpy $GameExePath "$GameRoot\Apps\SimCity 4.exe"

  ${IfNot} ${FileExists} "$GameExePath"
    MessageBox MB_OK|MB_ICONSTOP "Could not find '$GameExePath'.$\r$\n$\r$\n${APP_NAME} requires the SimCity 4 1.1.641.x executable in the selected game folder."
    Abort
  ${EndIf}

  ClearErrors
  GetDLLVersion "$GameExePath" $0 $1
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "Could not read the version information from '$GameExePath'.$\r$\n$\r$\n${APP_NAME} requires SimCity 4 version 1.1.641.x."
    Abort
  ${EndIf}

  IntOp $2 $0 >> 16
  IntOp $2 $2 & 0xFFFF
  IntOp $3 $0 & 0xFFFF
  IntOp $4 $1 >> 16
  IntOp $4 $4 & 0xFFFF
  IntOp $5 $1 & 0xFFFF

  ${If} $2 != 1
  ${OrIf} $3 != 1
  ${OrIf} $4 != 641
    MessageBox MB_OK|MB_ICONSTOP "Unsupported SimCity 4 version detected in '$GameExePath'.$\r$\n$\r$\nFound: $2.$3.$4.$5$\r$\nRequired: 1.1.641.x$\r$\n$\r$\nPlease install the 1.1.641 update before continuing."
    Abort
  ${EndIf}
FunctionEnd

Function WarnIfNo4GBPatch
  ClearErrors
  FileOpen $0 "$GameExePath" r
  ${If} ${Errors}
    Return
  ${EndIf}

  FileSeek $0 60 SET
  ClearErrors
  FileReadByte $0 $1
  FileReadByte $0 $2
  FileReadByte $0 $3
  FileReadByte $0 $4
  ${If} ${Errors}
    FileClose $0
    Return
  ${EndIf}

  IntOp $5 $2 << 8
  IntOp $5 $5 + $1
  IntOp $6 $3 << 16
  IntOp $5 $5 + $6
  IntOp $6 $4 << 24
  IntOp $5 $5 + $6

  IntOp $5 $5 + 22
  FileSeek $0 $5 SET
  ClearErrors
  FileReadByte $0 $1
  FileReadByte $0 $2
  ${If} ${Errors}
    FileClose $0
    Return
  ${EndIf}
  FileClose $0

  IntOp $3 $2 << 8
  IntOp $3 $3 + $1
  IntOp $3 $3 & 0x20

  ${If} $3 == 0
    MessageBox MB_OK|MB_ICONEXCLAMATION "The selected SimCity 4 executable does not appear to have the 4GB patch applied.$\r$\n$\r$\n${APP_NAME} can still be installed, but applying the 4GB patch is recommended for stability."
  ${EndIf}
FunctionEnd

Function WarnIfPluginsFolderEmpty
  ClearErrors
  FindFirst $0 $1 "$SC4PluginsDir\*"
  ${If} ${Errors}
    Return
  ${EndIf}

  StrCpy $2 "1"
  plugins_dir_scan:
    StrCmp $1 "." plugins_dir_next
    StrCmp $1 ".." plugins_dir_next
    StrCpy $2 "0"
    Goto plugins_dir_done
  plugins_dir_next:
    ClearErrors
    FindNext $0 $1
    ${IfNot} ${Errors}
      Goto plugins_dir_scan
    ${EndIf}

  plugins_dir_done:
  FindClose $0

  ${If} $2 == "1"
    MessageBox MB_OK|MB_ICONEXCLAMATION "The selected Plugins folder is empty:$\r$\n$SC4PluginsDir$\r$\n$\r$\nThis usually means the wrong folder was selected!"
  ${EndIf}
FunctionEnd

Function ValidateRenderServicesDependency
  StrCpy $RenderServicesDllPath "$SC4PluginsDir\${RENDER_SERVICES_DLL_NAME}"

  ${IfNot} ${FileExists} "$RenderServicesDllPath"
    MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} requires SC4RenderServices to be installed first.$\r$\n$\r$\nCould not find '$RenderServicesDllPath'.$\r$\n$\r$\nPlease install SC4RenderServices, then run this installer again.$\r$\n$\r$\nDownload page:$\r$\nhttps://community.simtropolis.com/files/file/37372-sc4-render-services/"
    Abort
  ${EndIf}

  ClearErrors
  GetDLLVersion "$RenderServicesDllPath" $0 $1
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONSTOP "Could not read the version information from '$RenderServicesDllPath'.$\r$\n$\r$\nPlease reinstall SC4RenderServices before continuing.$\r$\n$\r$\nProject page:$\r$\nhttps://github.com/caspervg/sc4-render-services"
    Abort
  ${EndIf}

  IntOp $2 $0 >> 16
  IntOp $2 $2 & 0xFFFF
  IntOp $3 $0 & 0xFFFF
  IntOp $4 $1 >> 16
  IntOp $4 $4 & 0xFFFF
  IntOp $5 $1 & 0xFFFF

  StrCpy $6 "0"
  ${If} $2 > ${MIN_RENDER_SERVICES_VERSION_MAJOR}
    StrCpy $6 "1"
  ${ElseIf} $2 == ${MIN_RENDER_SERVICES_VERSION_MAJOR}
    ${If} $3 > ${MIN_RENDER_SERVICES_VERSION_MINOR}
      StrCpy $6 "1"
    ${ElseIf} $3 == ${MIN_RENDER_SERVICES_VERSION_MINOR}
      ${If} $4 >= ${MIN_RENDER_SERVICES_VERSION_BUILD}
        StrCpy $6 "1"
      ${EndIf}
    ${EndIf}
  ${EndIf}

  ${If} $6 != "1"
    MessageBox MB_OK|MB_ICONSTOP "Unsupported SC4RenderServices version detected in '$RenderServicesDllPath'.$\r$\n$\r$\nFound: $2.$3.$4.$5$\r$\nRequired: ${MIN_RENDER_SERVICES_VERSION_MAJOR}.${MIN_RENDER_SERVICES_VERSION_MINOR}.${MIN_RENDER_SERVICES_VERSION_BUILD}.x or newer$\r$\n$\r$\nPlease update SC4RenderServices before continuing.$\r$\n$\r$\nProject page:$\r$\nhttps://github.com/caspervg/sc4-render-services"
    Abort
  ${EndIf}
FunctionEnd

Function ConfigurePathsPage
  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0u 0u 100% 18u "Choose where to install ${APP_NAME} files."
  ${NSD_CreateLabel} 0u 24u 100% 10u "SimCity 4 game root (contains Apps folder):"
  ${NSD_CreateDirRequest} 0u 36u 82% 12u "$GameRoot"
  Pop $HGameRoot
  ${NSD_CreateButton} 84% 36u 16% 12u "Browse..."
  Pop $HBrowseGameRoot
  ${NSD_OnClick} $HBrowseGameRoot OnBrowseGameRoot

  ${NSD_CreateLabel} 0u 56u 100% 10u "SimCity 4 Plugins directory:"
  ${NSD_CreateDirRequest} 0u 68u 82% 12u "$SC4PluginsDir"
  Pop $HPluginsDir
  ${NSD_CreateButton} 84% 68u 16% 12u "Browse..."
  Pop $HBrowsePluginsDir
  ${NSD_OnClick} $HBrowsePluginsDir OnBrowsePluginsDir

  nsDialogs::Show
FunctionEnd

Function OnBrowseGameRoot
  Pop $0
  nsDialogs::SelectFolderDialog "Select SimCity 4 game root folder" "$GameRoot"
  Pop $0
  ${If} $0 != error
    StrCpy $GameRoot $0
    ${NSD_SetText} $HGameRoot $GameRoot
  ${EndIf}
FunctionEnd

Function OnBrowsePluginsDir
  Pop $0
  nsDialogs::SelectFolderDialog "Select SimCity 4 Plugins folder" "$SC4PluginsDir"
  Pop $0
  ${If} $0 != error
    StrCpy $SC4PluginsDir $0
    ${NSD_SetText} $HPluginsDir $SC4PluginsDir
  ${EndIf}
FunctionEnd

Function WarnLegacyPlugins
  ; Build a list of legacy DLLs found in Plugins and Apps folders.
  ; NTFS is case-insensitive, so FileExists matches regardless of case.
  StrCpy $0 ""  ; accumulates found file paths
  StrCpy $1 "0" ; count

  !macro _CheckLegacyIn _dir _file
    ${If} ${FileExists} "${_dir}\${_file}"
      StrCpy $0 "$0$\r$\n  - ${_dir}\${_file}"
      IntOp $1 $1 + 1
    ${EndIf}
  !macroend

  !insertmacro _CheckLegacyIn "$SC4PluginsDir" "SC4AdvancedPlop.dll"
  !insertmacro _CheckLegacyIn "$SC4PluginsDir" "SC4ImGuiService.dll"
  !insertmacro _CheckLegacyIn "$SC4PluginsDir" "SC4ImguiService.dll"
  !insertmacro _CheckLegacyIn "$SC4PluginsDir" "SC4CustomServices.dll"
  !insertmacro _CheckLegacyIn "$GameRoot\Apps" "SC4AdvancedPlop.dll"
  !insertmacro _CheckLegacyIn "$GameRoot\Apps" "SC4ImGuiService.dll"
  !insertmacro _CheckLegacyIn "$GameRoot\Apps" "SC4ImguiService.dll"
  !insertmacro _CheckLegacyIn "$GameRoot\Apps" "SC4CustomServices.dll"

  ${If} $1 == "0"
    Return
  ${EndIf}

  MessageBox MB_YESNO|MB_ICONEXCLAMATION "The following legacy DLL mods were found:$\r$\n$0$\r$\n$\r$\nThese are from older versions and may conflict with ${APP_NAME}.$\r$\n$\r$\nDelete them now?" IDNO legacy_skip

  !macro _DeleteLegacyIn _dir _file
    ${If} ${FileExists} "${_dir}\${_file}"
      Delete "${_dir}\${_file}"
      DetailPrint "Deleted legacy ${_dir}\${_file}"
    ${EndIf}
  !macroend

  !insertmacro _DeleteLegacyIn "$SC4PluginsDir" "SC4AdvancedPlop.dll"
  !insertmacro _DeleteLegacyIn "$SC4PluginsDir" "SC4ImGuiService.dll"
  !insertmacro _DeleteLegacyIn "$SC4PluginsDir" "SC4ImguiService.dll"
  !insertmacro _DeleteLegacyIn "$SC4PluginsDir" "SC4CustomServices.dll"
  !insertmacro _DeleteLegacyIn "$GameRoot\Apps" "SC4AdvancedPlop.dll"
  !insertmacro _DeleteLegacyIn "$GameRoot\Apps" "SC4ImGuiService.dll"
  !insertmacro _DeleteLegacyIn "$GameRoot\Apps" "SC4ImguiService.dll"
  !insertmacro _DeleteLegacyIn "$GameRoot\Apps" "SC4CustomServices.dll"

  legacy_skip:
FunctionEnd

Function ConfigurePathsPageLeave
  ${NSD_GetText} $HGameRoot $GameRoot
  ${NSD_GetText} $HPluginsDir $SC4PluginsDir

  ${If} $GameRoot == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Game root cannot be empty."
    Abort
  ${EndIf}

  ${If} $SC4PluginsDir == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Plugins directory cannot be empty."
    Abort
  ${EndIf}

  ${GetParent} "$SC4PluginsDir" $SC4ToolsDir
  ${If} $SC4ToolsDir == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Could not determine parent folder of Plugins directory."
    Abort
  ${EndIf}
  StrCpy $SC4ToolsDir "$SC4ToolsDir\${APP_TOOLS_SUBDIR}"

  ${IfNot} ${FileExists} "$GameRoot\Apps\*.*"
    MessageBox MB_OK|MB_ICONSTOP "Could not find '$GameRoot\Apps'.$\r$\n$\r$\nPlease select your SimCity 4 game root folder (the folder that contains 'Apps')."
    Abort
  ${EndIf}

  Call ValidateGameExecutable
  Call WarnIfNo4GBPatch
  Call WarnIfPluginsFolderEmpty
  Call ValidateRenderServicesDependency
  Call WarnLegacyPlugins
FunctionEnd

Function ConfigureCachePage
  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0u 0u 100% 20u "Cache-build settings. Output will be shown in the installer details window."
  ${NSD_CreateLabel} 0u 28u 100% 10u "Locale under game root (found automatically):"
  ${NSD_CreateDropList} 0u 40u 100% 70u ""
  Pop $HCacheLocale
  Call PopulateLocaleDropDown

  ${NSD_CreateCheckbox} 0u 72u 100% 12u "Render 3D thumbnails (slower)"
  Pop $HCacheRenderThumbs
  ${If} $CacheRenderThumbs == "1"
    ${NSD_Check} $HCacheRenderThumbs
  ${EndIf}

  ${NSD_CreateLabel} 0u 90u 100% 10u "Thumbnail size in pixels (22-176, used for cache thumbnails and UI display):"
  ${NSD_CreateText} 0u 102u 100% 12u "$CacheThumbnailSize"
  Pop $HCacheThumbnailSize

  ${NSD_CreateCheckbox} 0u 122u 100% 12u "Build cache during installation"
  Pop $HCacheBuildNow
  ${If} $CacheBuildNow == "1"
    ${NSD_Check} $HCacheBuildNow
  ${EndIf}

  nsDialogs::Show
FunctionEnd

Function ConfigureCachePageLeave
  ${NSD_GetText} $HCacheLocale $CacheLocale
  ${NSD_GetText} $HCacheThumbnailSize $CacheThumbnailSize
  ${NSD_GetState} $HCacheRenderThumbs $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $CacheRenderThumbs "1"
  ${Else}
    StrCpy $CacheRenderThumbs "0"
  ${EndIf}

  ${If} $CacheLocale == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Locale cannot be empty."
    Abort
  ${EndIf}

  ${If} $CacheThumbnailSize == ""
    MessageBox MB_OK|MB_ICONEXCLAMATION "Thumbnail size cannot be empty."
    Abort
  ${EndIf}

  IntCmp $CacheThumbnailSize 22 0 thumbnail_size_too_small thumbnail_size_ok_min
  thumbnail_size_too_small:
    MessageBox MB_OK|MB_ICONEXCLAMATION "Thumbnail size must be an integer between 22 and 176 pixels."
    Abort
  thumbnail_size_ok_min:
  IntCmp $CacheThumbnailSize 176 thumbnail_size_ok_max thumbnail_size_ok_max thumbnail_size_too_large
  thumbnail_size_too_large:
    MessageBox MB_OK|MB_ICONEXCLAMATION "Thumbnail size must be an integer between 22 and 176 pixels."
    Abort
  thumbnail_size_ok_max:

  ${NSD_GetState} $HCacheBuildNow $0
  ${If} $0 == ${BST_CHECKED}
    StrCpy $CacheBuildNow "1"
  ${Else}
    StrCpy $CacheBuildNow "0"
  ${EndIf}
FunctionEnd

Function PopulateLocaleDropDown
  StrCpy $0 "0"
  StrCpy $3 "0"
  FindFirst $1 $2 "$GameRoot\*"
  ${If} ${Errors}
    Goto locale_done
  ${EndIf}

  locale_loop:
    StrCmp $2 "." locale_next
    StrCmp $2 ".." locale_next
    IfFileExists "$GameRoot\$2\*.*" 0 locale_next
    IfFileExists "$GameRoot\$2\SimCityLocale.DAT" 0 locale_next
    ${NSD_CB_AddString} $HCacheLocale $2
    StrCpy $0 "1"
    ${If} $2 == $CacheLocale
      StrCpy $3 "1"
    ${EndIf}
  locale_next:
    FindNext $1 $2
    ${IfNot} ${Errors}
      Goto locale_loop
    ${EndIf}

  FindClose $1

  locale_done:
  ${If} $0 == "0"
    ${NSD_CB_AddString} $HCacheLocale "$CacheLocale"
  ${ElseIf} $3 == "0"
    ${NSD_CB_AddString} $HCacheLocale "$CacheLocale"
  ${EndIf}
  ${NSD_CB_SelectString} $HCacheLocale "$CacheLocale"
FunctionEnd

Function ConfigureSummaryPage
  nsDialogs::Create 1018
  Pop $Dialog
  ${If} $Dialog == error
    Abort
  ${EndIf}

  ${NSD_CreateLabel} 0u 0u 100% 12u "Review settings before installation:"

  ${If} $CacheRenderThumbs == "1"
    StrCpy $0 "Yes"
  ${Else}
    StrCpy $0 "No"
  ${EndIf}

  ${If} $CacheBuildNow == "1"
    StrCpy $1 "Yes"
  ${Else}
    StrCpy $1 "No"
  ${EndIf}

  ${NSD_CreateLabel} 0u 16u 100% 88u "Game root:$\r$\n$GameRoot$\r$\n$\r$\nPlugins dir:$\r$\n$SC4PluginsDir$\r$\n$\r$\nTools/output dir:$\r$\n$SC4ToolsDir$\r$\n$\r$\nCache locale: $CacheLocale$\r$\nRender 3D thumbnails: $0$\r$\nThumbnail size: $CacheThumbnailSize px$\r$\nBuild cache during install: $1"
  Pop $HSummaryText

  nsDialogs::Show
FunctionEnd

Section "Install"
  SetShellVarContext current

  CreateDirectory "$SC4PluginsDir"
  SetOutPath "$SC4PluginsDir"
  File "SC4PlopAndPaint.dll"
  File "SC4PlopAndPaint.dat"
  SetOverwrite off
  File "SC4PlopAndPaint.ini"
  SetOverwrite on
  WriteINIStr "$SC4PluginsDir\SC4PlopAndPaint.ini" "SC4PlopAndPaint" "ThumbnailDisplaySize" "$CacheThumbnailSize"

  CreateDirectory "$SC4ToolsDir"
  SetOutPath "$SC4ToolsDir"
  File "PropertyMapper.xml"
  File "_SC4PlopAndPaintCacheBuilder.exe"
  File "..\LICENSE.txt"
  File "THIRD_PARTY_NOTICES.txt"

  WriteUninstaller "$SC4ToolsDir\Uninstall-SC4PlopAndPaint.exe"

  WriteRegStr HKCU "${APP_REG_KEY}" "GameRoot" "$GameRoot"
  WriteRegStr HKCU "${APP_REG_KEY}" "PluginsDir" "$SC4PluginsDir"
  WriteRegStr HKCU "${APP_REG_KEY}" "ToolsDir" "$SC4ToolsDir"

  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME} ${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "SC4 Plop and Paint"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$SC4ToolsDir"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" "$\"$SC4ToolsDir\Uninstall-SC4PlopAndPaint.exe$\""
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1

  ; Generate a reusable cache rebuild script with the exact installation settings.
  StrCpy $2 "$SC4ToolsDir\Rebuild-Cache.ps1"
  FileOpen $3 $2 w
  FileWrite $3 "# Auto-generated by ${APP_NAME} installer.$\r$\n"
  FileWrite $3 "$$ErrorActionPreference = 'Stop'$\r$\n"
  FileWrite $3 "$$exe = Join-Path $$PSScriptRoot '_SC4PlopAndPaintCacheBuilder.exe'$\r$\n"
  FileWrite $3 "$$game = '$GameRoot'.TrimEnd('\')$\r$\n"
  FileWrite $3 "$$plugins = '$SC4PluginsDir'.TrimEnd('\')$\r$\n"
  FileWrite $3 "$$locale = '$CacheLocale'.TrimEnd('\')$\r$\n"
  FileWrite $3 "$$thumbnailSize = '$CacheThumbnailSize'$\r$\n"
  FileWrite $3 "$$cacheArgs = @('--scan', '--game', $$game, '--plugins', $$plugins, '--locale', $$locale, '--thumbnail-size', $$thumbnailSize)$\r$\n"
  ${If} $CacheRenderThumbs == "1"
    FileWrite $3 "$$cacheArgs += '--render-thumbnails'$\r$\n"
  ${EndIf}
  FileWrite $3 "$$logFile = Join-Path $$PSScriptRoot 'cache_build.log'$\r$\n"
  FileWrite $3 "$$renderThumbs = '$CacheRenderThumbs'$\r$\n"
  FileWrite $3 "$$header = @($\r$\n"
  FileWrite $3 "  'SC4 Plop and Paint Cache Build Log',$\r$\n"
  FileWrite $3 "  'Installer version: ${APP_VERSION}',$\r$\n"
  FileWrite $3 "  ('Timestamp (local): ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')),$\r$\n"
  FileWrite $3 "  ('Game root: ' + $$game),$\r$\n"
  FileWrite $3 "  ('Plugins dir: ' + $$plugins),$\r$\n"
  FileWrite $3 "  ('Locale: ' + $$locale),$\r$\n"
  FileWrite $3 "  ('Render thumbnails: ' + $$renderThumbs),$\r$\n"
  FileWrite $3 "  ('Thumbnail size: ' + $$thumbnailSize + ' px'),$\r$\n"
  FileWrite $3 "  ('Command: ' + $$exe + ' ' + ($$cacheArgs -join ' ')),$\r$\n"
  FileWrite $3 "  ''$\r$\n"
  FileWrite $3 ")$\r$\n"
  FileWrite $3 "$$header | Set-Content -Path $$logFile -Encoding UTF8$\r$\n"
  FileWrite $3 "$$header | ForEach-Object { Write-Host $$_ }$\r$\n"
  FileWrite $3 "& $$exe @cacheArgs 2>&1 | ForEach-Object { $$line = [string]$$_; Write-Host $$line; Add-Content -Path $$logFile -Value $$line -Encoding UTF8 }$\r$\n"
  FileWrite $3 "$$exitCode = $$LASTEXITCODE$\r$\n"
  FileWrite $3 "$$exitLine = 'Exit code: ' + $$exitCode$\r$\n"
  FileWrite $3 "Write-Host $$exitLine$\r$\n"
  FileWrite $3 "Add-Content -Path $$logFile -Value $$exitLine -Encoding UTF8$\r$\n"
  FileWrite $3 "exit $$exitCode$\r$\n"
  FileClose $3

  ${If} $CacheBuildNow == "1"
    DetailPrint "Building SC4 Plop and Paint cache..."
    DetailPrint "Using script: $SC4ToolsDir\Rebuild-Cache.ps1"
    StrCpy $0 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$SC4ToolsDir\Rebuild-Cache.ps1"'
    nsExec::ExecToLog $0
    Pop $1
    ${If} $1 != "0"
      MessageBox MB_ICONEXCLAMATION|MB_YESNO "Cache build failed (exit code: $1). Continue installation anyway?" IDYES +2
      Abort
    ${EndIf}
  ${EndIf}
SectionEnd

Function OpenToolsFolder
  SetShellVarContext current
  ExecShell "open" "$SC4ToolsDir"
FunctionEnd

Function un.onInit
  SetShellVarContext current
  ReadRegStr $GameRoot HKCU "${APP_REG_KEY}" "GameRoot"
  ReadRegStr $SC4PluginsDir HKCU "${APP_REG_KEY}" "PluginsDir"
  ReadRegStr $SC4ToolsDir HKCU "${APP_REG_KEY}" "ToolsDir"

  ${If} $GameRoot == ""
    ; SC4 is a 32-bit app, so read from the 32-bit registry view.
    StrCpy $GameRoot "$PROGRAMFILES32\SimCity 4 Deluxe Edition"
    SetRegView 32
    ReadRegStr $0 HKLM "SOFTWARE\Maxis\SimCity 4" "Install Dir"
    ${If} $0 != ""
      StrCpy $GameRoot $0
    ${EndIf}
  ${EndIf}
  ${If} $SC4PluginsDir == ""
    StrCpy $SC4PluginsDir "$DOCUMENTS\SimCity 4\Plugins"
  ${EndIf}
  ${If} $SC4ToolsDir == ""
    ${GetParent} "$SC4PluginsDir" $SC4ToolsDir
    StrCpy $SC4ToolsDir "$SC4ToolsDir\${APP_TOOLS_SUBDIR}"
  ${EndIf}
FunctionEnd

Section "Uninstall"
  SetShellVarContext current

  Delete "$SC4PluginsDir\SC4PlopAndPaint.dll"
  Delete "$SC4PluginsDir\SC4PlopAndPaint.dat"

  Delete "$SC4ToolsDir\PropertyMapper.xml"
  Delete "$SC4ToolsDir\_SC4PlopAndPaintCacheBuilder.exe"
  Delete "$SC4ToolsDir\LICENSE.txt"
  Delete "$SC4ToolsDir\THIRD_PARTY_NOTICES.txt"
  Delete "$SC4ToolsDir\Rebuild-Cache.ps1"
  Delete "$SC4ToolsDir\cache_build.log"
  Delete "$SC4ToolsDir\Uninstall-SC4PlopAndPaint.exe"
  RMDir "$SC4ToolsDir"

  MessageBox MB_YESNO|MB_ICONQUESTION "Also remove generated cache files (lots.cbor, props.cbor, flora.cbor, and thumbnail .bin sidecars)?" IDNO +7
  Delete "$SC4PluginsDir\lots.cbor"
  Delete "$SC4PluginsDir\props.cbor"
  Delete "$SC4PluginsDir\flora.cbor"
  Delete "$SC4PluginsDir\lot_thumbnails.bin"
  Delete "$SC4PluginsDir\prop_thumbnails.bin"
  Delete "$SC4PluginsDir\flora_thumbnails.bin"

  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_REG_KEY}"
SectionEnd
