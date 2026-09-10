# Changelog

## [Unreleased]

### Added
- Added crosshair suppression: the game's own crosshair is switched off on the frames the mod is marking the shot itself, and switched back on for every other frame, so there is one reticle on screen and it is the one that says where the round goes.
- Added window centring when playing windowed. BioShock Infinite opens its window against the top-left corner of the screen and leaves it there; the mod re-centres it on the monitor it is on whenever the game gives the window a new size, so a resolution change in the options menu is centred too. A window that fills the screen, one the game centred itself, and one you have dragged somewhere yourself are all left where they are.
- Added zoom compensation, so raising the sights no longer makes head tracking feel more sensitive. Yaw, pitch and the lean are scaled by the ratio of the half-field tangents, so a head movement shifts the picture by the same amount on screen whatever field of view the game is rendering. Roll is left alone.
- Head tracking for BioShock Infinite over the OpenTrack UDP protocol, with rotation and position applied to the rendered view only, so aim, shots and game logic keep the game's own camera.
- Hotkeys on the nav cluster and on Ctrl+Shift chords: toggle tracking, cycle tracking mode (full, rotation only, position only), toggle world-locked or camera-local yaw, and cycle ADS mode.
- Three aim-down-sights modes: tracking paused, tracking on with an aim marker drawn along the direction the weapon is pointing, and tracking on with no marker. The choice is saved to the INI on every press.
- Aim marker drawn over the frame, placed from the projection the frame was really drawn with, so it is right on any display shape including ultrawide.
- Field of view override in the INI, applied to the rendered frame only, with the angles written to HeadTracking.log.
- Per-build fingerprinting, so the mod stays dormant on a game build it does not recognise.
- HeadTracking.log beside the game exe, holding the launch in progress and nothing else: each launch keeps the one before it as HeadTracking.prev.log and starts the live file empty, so neither file grows over a session or across sessions.
- [Diagnostics] StateProbe in the INI, off by default. It dumps the player controller to the log every two seconds, and it is the only setting in the mod that writes to the log faster than a player can read it.

### Changed
- Changed `[Diagnostics] StateProbe` to write named settings with their live values rather than only hex, and to cover the crosshair and the player's pawn as well as the player controller.

### Fixed
- Fixed head tracking carrying on in menus. The pause menu, the map, the gear screen and the options now all suppress tracking; previously only the front end and level transitions did, and the pause menu kept swinging the camera around behind it.
- Fixed the crosshair search spending its retry backoff at the front end, where the widget cannot exist. It now starts the moment the HUD exists, rather than leaving the game's crosshair in the middle of the screen for the half minute until the next attempt came around.
- Fixed the log blaming a failed tracker port bind on another app listening. It now reports the reason the OS gave.
