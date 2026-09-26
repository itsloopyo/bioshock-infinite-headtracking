# Changelog

## [Unreleased]

### Added
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- Added window centring when playing windowed. BioShock Infinite opens its window against the top-left corner of the screen and leaves it there; the mod re-centres it on the monitor it is on whenever the game gives the window a new size, so a resolution change in the options menu is centred too. A window that fills the screen, one the game centred itself, and one you have dragged somewhere yourself are all left where they are.
- Added zoom compensation. While the game renders a narrower field of view than its unzoomed one, yaw, pitch and the lean are scaled by the ratio of the two half-field tangents, which is meant to keep a head movement worth the same distance on screen whatever field of view the game is rendering. Roll is left alone.
- Head tracking for BioShock Infinite over the OpenTrack UDP protocol, with rotation and position applied to the rendered view only, so aim, shots and game logic keep the game's own camera.
- Hotkeys on the nav cluster and on Ctrl+Shift chords: toggle tracking, cycle tracking mode (full, rotation only, position only) and toggle world-locked or camera-local yaw.
- The game's own crosshair is moved to follow the aim, placed from the projection the frame was really drawn with, so it is right on any display shape including ultrawide. The mod draws no reticle of its own.
- Field of view override in `CameraUnlock.ini`, applied to the rendered frame only, with the angles written to HeadTracking.log.
- Per-build fingerprinting, so the mod stays dormant on a game build it does not recognise.
- HeadTracking.log beside the game exe, holding the launch in progress and nothing else: each launch keeps the one before it as HeadTracking.prev.log and starts the live file empty, so neither file grows over a session or across sessions.
- [Diagnostics] StateProbe in `CameraUnlock.ini`, off by default. It dumps the player controller to the log every two seconds, and it is the only setting in the mod that writes to the log faster than a player can read it.

### Changed
- Settings move to `Binaries\Win32\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. The old `Toggle`, `CycleMode` and `YawMode` codes and their `ChordToggle`, `ChordCycleMode` and `ChordYawMode` switches are imported into those lists.
- The tracking mode (`Page Up` / `Ctrl+Shift+G`) and the yaw mode (`Page Down` / `Ctrl+Shift+H`) are saved to `CameraUnlock.ini` when you switch them, as `RotationEnabled`, `PositionEnabled` and `WorldSpaceYaw`, so the game starts in the mode you left it in. `End` still switches tracking for the session only.
- `[Position] Enabled` is now `[Position] PositionEnabled` with `[General] RotationEnabled`, the tracking mode the game starts in; the other keys take their canonical names too (`[Network] UdpPort`, `PositionLimitX` and so on). The import writes the value your old file had under each new name.
- A `HeadTracking.ini` holding a position limit above 10 metres is not imported: `CameraUnlock.ini` takes limits from 0 to 10. The mod runs on the values the file holds, saves nothing, creates no `CameraUnlock.ini`, and names the key in `HeadTracking.log` at every start until the value is 10 or less.
- `uninstall.cmd` now leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so your settings survive a reinstall. It used to delete `HeadTracking.ini`.
- Changed `[Diagnostics] StateProbe` to write named settings with their live values rather than only hex, and to cover the crosshair and the player's pawn as well as the player controller.

### Removed
- `[General] ShowAimMarker`, which could stop the game's crosshair following the aim. It always follows the aim now, and a file that turned it off is imported without it.
- The aim-down-sights mode cycle (78cb86e). The dev build of 2026-09-16 paused head tracking while the sights were up by default, bound Insert and Ctrl+Shift+U to cycle three modes, and saved the choice as `[General] AdsMode`. Head tracking now stays on through the aim, neither key is bound, and `[General] AdsMode`, `[Hotkeys] AdsMode` and `[Hotkeys] ChordAdsMode` are not carried into `CameraUnlock.ini`.

### Fixed
- Fixed head tracking carrying on in menus. The pause menu, the map, the gear screen and the options now all suppress tracking; previously only the front end and level transitions did, and the pause menu kept swinging the camera around behind it.
- Fixed the crosshair search spending its retry backoff at the front end, where the widget cannot exist. It now starts the moment the HUD exists, rather than leaving the game's crosshair in the middle of the screen for the half minute until the next attempt came around.
- Fixed the log blaming a failed tracker port bind on another app listening. It now reports the reason the OS gave.
