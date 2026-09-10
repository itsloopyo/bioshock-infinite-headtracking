@echo off
:: ============================================
:: BioShock Infinite Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-shim.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/install-wrapper-shim.cmd. Copy this
:: file to <mod>/scripts/install.cmd, fill in the CONFIG BLOCK, change nothing
:: else. scripts/conformance.ps1 checks that nothing else changed.
::
:: Shim-only: the mod DLL is itself a system-DLL proxy the game loads
:: directly, so there is no framework to install. Any pre-existing DLL of that
:: name is preserved as <name>.backup on first install for uninstall to
:: restore, which is why FRAMEWORK_TYPE is None.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=bioshock-infinite"
set "MOD_DISPLAY_NAME=BioShock Infinite Head Tracking"
set "MOD_DLLS=xinput1_3.dll"
set "MOD_INTERNAL_NAME=BioShockInfiniteHeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=None"
:: Files copied only when they are not already there, so an upgrade keeps
:: whatever the user tuned. Listing an .ini in MOD_DLLS instead puts it through
:: the unconditional copy and the shim byte compare, which resets every key on
:: every update and then records the tuned file as the game original.
set "MOD_SEED_FILES="
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls (nav cluster / chord):&echo   End      / Ctrl+Shift+Y  Toggle head tracking&echo   PageUp   / Ctrl+Shift+G  Cycle tracking mode: full, rotation only, position only&echo   PageDown / Ctrl+Shift+H  Toggle yaw mode: world-locked or camera-local&echo   Insert   / Ctrl+Shift+U  Cycle ADS mode: paused, aim marker, tracked"
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-shim.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-shim.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-shim.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
