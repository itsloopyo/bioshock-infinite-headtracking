# Changelog

## [Unreleased]

### Added
- Added window centring when playing windowed. BioShock Infinite opens its window against the top-left corner of the screen and leaves it there; the mod re-centres it on the monitor it is on whenever the game gives the window a new size, so a resolution change in the options menu is centred too. A window that fills the screen, one the game centred itself, and one you have dragged somewhere yourself are all left where they are.
- Added zoom compensation. While the game renders a narrower field of view than its unzoomed one, yaw, pitch and the lean are scaled by the ratio of the two half-field tangents, which is meant to keep a head movement worth the same distance on screen whatever field of view the game is rendering. Roll is left alone.
- Head tracking for BioShock Infinite over the OpenTrack UDP protocol, with rotation and position applied to the rendered view only, so aim, shots and game logic keep the game's own camera.
- Hotkeys on the nav cluster and on Ctrl+Shift chords: toggle tracking, cycle tracking mode (full, rotation only, position only) and toggle world-locked or camera-local yaw.
- The game's own crosshair is moved to follow the aim, placed from the projection the frame was really drawn with, so it is right on any display shape including ultrawide. The mod draws no reticle of its own.
- Field of view override in the INI, applied to the rendered frame only, with the angles written to HeadTracking.log.
- Per-build fingerprinting, so the mod stays dormant on a game build it does not recognise.
- HeadTracking.log beside the game exe, holding the launch in progress and nothing else: each launch keeps the one before it as HeadTracking.prev.log and starts the live file empty, so neither file grows over a session or across sessions.
- [Diagnostics] StateProbe in the INI, off by default. It dumps the player controller to the log every two seconds, and it is the only setting in the mod that writes to the log faster than a player can read it.

### Changed
- `HeadTracking.ini` has a new layout. The first time this version starts, it converts the file once into the new layout and keeps the file as it was beside it as `HeadTracking.ini.pre-canonical`. `HeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. The old `Toggle`, `CycleMode` and `YawMode` codes and their `ChordToggle`, `ChordCycleMode` and `ChordYawMode` switches are converted into those lists.
- An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `HeadTracking.ini.pre-canonical` back over `HeadTracking.ini`, which restores the old file.
- The tracking mode (`Page Up` / `Ctrl+Shift+G`) and the yaw mode (`Page Down` / `Ctrl+Shift+H`) are saved to `HeadTracking.ini` when you switch them, as `RotationEnabled`, `PositionEnabled` and `WorldSpaceYaw`, so the game starts in the mode you left it in. `End` still switches tracking for the session only.
- `[Position] Enabled` is now `[Position] PositionEnabled` with `[General] RotationEnabled`, the tracking mode the game starts in; the other keys take their canonical names too (`[Network] UdpPort`, `PositionLimitX` and so on). The conversion writes the value your old file had under each new name.
- A `HeadTracking.ini` holding a position limit above 10 metres is not converted: the new layout's limits run from 0 to 10. The mod runs on the file as it is, saves nothing, and names the key in `HeadTracking.log` at every start until the value is 10 or less.
- `uninstall.cmd` now leaves `HeadTracking.ini` and its `.pre-canonical` copies in place, so your settings survive a reinstall. It used to delete the file.
- Changed `[Diagnostics] StateProbe` to write named settings with their live values rather than only hex, and to cover the crosshair and the player's pawn as well as the player controller.

### Removed
- `[General] ShowAimMarker`, which could stop the game's crosshair following the aim. It always follows the aim now, and a file that turned it off converts without it.
- The aim-down-sights mode cycle (78cb86e). The dev build of 2026-09-16 paused head tracking while the sights were up by default, bound Insert and Ctrl+Shift+U to cycle three modes, and saved the choice as `[General] AdsMode`. Head tracking now stays on through the aim, neither key is bound, and `[General] AdsMode`, `[Hotkeys] AdsMode` and `[Hotkeys] ChordAdsMode` are not carried into the new file.

### Fixed
- Fixed head tracking carrying on in menus. The pause menu, the map, the gear screen and the options now all suppress tracking; previously only the front end and level transitions did, and the pause menu kept swinging the camera around behind it.
- Fixed the crosshair search spending its retry backoff at the front end, where the widget cannot exist. It now starts the moment the HUD exists, rather than leaving the game's crosshair in the middle of the screen for the half minute until the next attempt came around.
- Fixed the log blaming a failed tracker port bind on another app listening. It now reports the reason the OS gave.
