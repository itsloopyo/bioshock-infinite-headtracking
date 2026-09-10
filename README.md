# BioShock Infinite Head Tracking

![BioShock Infinite running with this mod](https://raw.githubusercontent.com/itsloopyo/bioshock-infinite-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for BioShock Infinite that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - your head moves the view; the mouse or controller still moves the aim
- **6DOF positional tracking** - lean in, lean out, lean around a corner
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- A purchased copy of [BioShock Infinite](https://store.steampowered.com/app/8870/BioShock_Infinite/).
  The mod knows one build, the 2022-05-11 Steam one, and checks the executable before it
  hooks anything. On any other build - a later Steam patch, or the GOG release, which
  `install.cmd` will still install into - it stays dormant and names the mismatch in
  `HeadTracking.log`.
- A tracking source that speaks OpenTrack UDP: [OpenTrack](https://github.com/opentrack/opentrack/releases)
  with a webcam or hardware tracker, or a phone app that sends the same packet to port 4242.
- Windows 10 or 11. The mod is 32-bit, because the game is.

## Installation

1. Download the latest `BioShockInfiniteHeadTracking-v<version>-installer.zip` from
   [Releases](https://github.com/itsloopyo/bioshock-infinite-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds your Steam or GOG install and copies
   `xinput1_3.dll` into the game's `Binaries\Win32\` folder, keeping any file already of
   that name as `xinput1_3.dll.backup` for `uninstall.cmd` to put back.
4. Configure OpenTrack to send **UDP over network** to `127.0.0.1`, port `4242`. See
   [Setting Up OpenTrack](#setting-up-opentrack).
5. Launch the game.

**If the installer cannot find your game**, tell it where the game is. Either pass the
folder as an argument:

```powershell
install.cmd "D:\Games\BioShock Infinite"
```

or set the `BIOSHOCK_INFINITE_PATH` environment variable to the game's installation folder
before running it:

```powershell
$env:BIOSHOCK_INFINITE_PATH = "D:\Games\BioShock Infinite"
```

**Mod managers do not deploy this mod.** The payload has to sit next to
`BioShockInfinite.exe`. Vortex ships no extension for this game, so it cannot manage it at
all. Mod Organizer 2 deploys into the game's `Data` folder unless Root Builder is enabled,
so the file would land somewhere the game never looks: the manager would report a
successful install, and the game would start normally with no head tracking and no log.
Use `install.cmd`, or copy the file yourself.

### Manual Installation

The installer ZIP contains `plugins\xinput1_3.dll`. Copy that one file into:

```
<Steam library>\steamapps\common\BioShock Infinite\Binaries\Win32\
```

The game imports `XINPUT1_3.dll` and Windows searches the exe's own folder before the
system one, so putting it there is the whole installation. There is no separate mod loader
to install. The mod forwards every XInput call on to the real system DLL, so controllers
keep working as long as that copy is present - the log names it if it is not. To remove
it, delete the file.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network interface.
One datagram is six little-endian 64-bit floats in the order `x, y, z, yaw, pitch, roll`:
position in centimeters, rotation in degrees, 48 bytes in total. Anything that sends that
to that port drives the view. OpenTrack's **UDP over network** output sends exactly this,
and the steps below set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### VR Headset Setup

1. Connect the headset to the PC over Air Link, Virtual Desktop, or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on **UDP over network**, host `127.0.0.1`, port `4242`.

### Webcam Setup

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam, with no markers and
no IR hardware required. Select it under **Input**, pick your camera in its settings, and
use the output settings above. How well it tracks depends on your camera and your
lighting, so try it before buying anything.

### Phone App Setup

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends the
datagram described above. Point it at this PC's IP address (run `ipconfig` to find it) on
port `4242`. Not every phone tracker speaks this protocol, so check yours for an OpenTrack
or UDP output option first.

Sending direct works when the app filters its own signal on the device. The mod's
smoothing is sized to take the edge off a clean signal rather than to rescue a noisy one,
so an unfiltered feed sent direct may jitter. The test is quick: send direct, hold your head
still, and if the view drifts or shakes, route the app through OpenTrack instead. Point it
at OpenTrack's **UDP over network** *input* on some other port, say 5252, and let
OpenTrack's filters and curves clean the feed up before its output forwards to
`127.0.0.1:4242`. Going through OpenTrack is also how you get its curve mapping on an app
that has none of its own.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone
already in their pocket. It filters on-device, so it is one of the apps that can send
direct. Any app that filters enough noise works exactly the same way.

A phone on WiFi is a remote connection, so it is smoothed with `RemoteSmoothing` rather
than `LocalSmoothing`. So is a tracker running on this very PC that sends to the machine's
own LAN address instead of `127.0.0.1`: the mod reads the source address, not the machine.

### Centering

Centering is done in your tracker. Press OpenTrack's **Center** bind, the CENTER button
in your phone app, or SteamVR's reset, and the tracker zeroes its own output, which
leaves the view centered.

## Controls

Head tracking pauses automatically in the title, main and pause menus, and resumes
when you return to gameplay.

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |
| Cycle ADS mode      | `Insert`    | `Ctrl+Shift+U`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches yaw between world-locked (horizon-locked) and camera-local.

`Insert` / `Ctrl+Shift+U` cycles what happens when you aim down sights. All three start
the same way - raising the sights swings the view onto the point the reticle was marking,
so your shot lands where you had it lined up - and they differ in what happens for the
rest of the aim:

1. **Tracking paused** (default) - the game keeps the camera for as long as the sights are
   up. The sight picture is exactly the game's, and head movement does nothing until you
   lower the weapon.
2. **Tracking on, with reticle correction** (`marker`) - head tracking carries on
   from the snapped position. The stock crosshair moves with the projected aim direction
   whenever the game displays it. No extra crosshair is drawn.
3. **Tracking on, without reticle correction** (`tracked`) - head tracking carries on
   while the stock crosshair keeps its native position.

The choice is saved to the INI on every press, so it survives a restart, and the mode you
switched to is named in `HeadTracking.log` beside the game exe.

Every key is remappable in the `[Hotkeys]` section of the mod's INI, and each chord can be
disabled there independently of its nav-cluster key.

## Configuration

Settings live in `HeadTracking.ini`, written into `Binaries\Win32\` next to the game exe
the first time the mod runs. A key missing from the file falls back to its default, so an
INI written by an older build keeps working and simply picks up the defaults for anything
new.

```ini
[General]
EnableOnStartup=true
; UDP port the mod listens on for OpenTrack pose data.
Port=4242
; Pose older than this is treated as stale, and the view holds where it was.
DataFreshnessMs=500
; Yaw mode: true = horizon-locked yaw (default), false = camera-local.
WorldSpaceYaw=true
; Move the stock crosshair to follow the weapon's aim while your head turns.
; No extra reticle is drawn. False leaves the stock position unchanged.
ShowAimMarker=true
; What head tracking does while the sights are up: paused, marker, or tracked.
; Cycled in game with Insert (or Ctrl+Shift+U), which writes the value back here.
AdsMode=paused

[View]
; Field of view in degrees, or 0 to render the game's own. Valid values are 30 to 150.
Fov=0

[Smoothing]
; 0.0 (responsive) to 1.0 (heavy). Covers rotation and position. Which value
; applies is picked per connection from the packet source address: loopback
; gets LocalSmoothing, anything else gets RemoteSmoothing.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
; 6DOF positional tracking. The limits are meters of head travel, not a
; sensitivity: they bound how far the view may lean.
Enabled=true
LimitX=0.30
; Vertical travel is clamped to [-LimitYDown, +LimitY].
LimitY=0.20
LimitYDown=0.20
; Asymmetric: more room to lean forward than to pull back through your own body.
LimitZ=0.40
LimitZBack=0.10

[Hotkeys]
; Virtual-key codes. Defaults: End, Page Up, Page Down, Insert.
Toggle=0x23
CycleMode=0x21
YawMode=0x22
AdsMode=0x2D
; Each chord can be turned off independently of its nav-cluster key.
ChordToggle=true
ChordCycleMode=true
ChordYawMode=true
ChordAdsMode=true

[Diagnostics]
; Dumps the player controller to the log every two seconds. A maintenance
; diagnostic: it buries everything else in the log. Leave it false.
StateProbe=false
; Samples the head pose, the aim and where it landed on screen every two
; seconds. Turn it on only while reporting a misplaced aim marker.
AimGeometry=false
```

Pose shaping belongs to the tracker. Set sensitivity, deadzones, response curves and axis
inversion once in OpenTrack or your phone app, and one profile then behaves the same in
every game.

### Yaw mode

World-locked yaw keeps head yaw on the world up-axis however the camera is pitched, so
looking at the floor and turning your head pans across it. Camera-local yaw turns about
the camera's own up-axis instead, which leans and rolls the view at steep pitches.
`Page Down` / `Ctrl+Shift+H` switches between the two at any time; the INI value is the
mode the mod starts in.

### Field of view

The game's own Field of View slider is not a number of degrees, and its whole travel is
worth only a few percent: at maximum it measured 82.5 degrees against the camera's 75
degree reference. `[View] Fov` is there for anything wider.

The number you set is the horizontal angle on a 16:9 display. The game holds its vertical
field to that reference and widens the horizontal one with your aspect ratio, so a wider
screen shows more to the sides rather than less top and bottom, and the same number means
the same view on any of them.

Only the rendered frame is widened, and it is applied as a ratio against the angle the
camera renders at when nothing is zooming. Iron sights, scopes and scripted cameras still
zoom by the same factor they always did, and shots, traces and aim assist keep the game's
own angle.

## Troubleshooting

Every answer below starts in the same place: `HeadTracking.log`, written next to the game
exe in `Binaries\Win32\`. Each launch keeps the previous one as `HeadTracking.prev.log`
and starts the live file empty, so neither grows over time.

**Mod not loading: nothing happens at all, and there is no log file**

- The DLL is not next to `BioShockInfinite.exe`. Check for
  `Binaries\Win32\xinput1_3.dll`. If you used a mod manager, see the note under
  Installation: managers cannot deploy this mod.
- The log says the build was not recognized. The mod pins itself to a specific shipped
  build of the game and stays completely dormant on one it does not know, rather than
  hooking against addresses that have moved. That line says whether your game is newer or
  older than the build the mod knows, and prints your exe's fingerprint. Include it in a
  bug report and it is enough to add support for your build.

**No tracking response: the mod loads, but the view never moves**

- The log says no tracker packets are arriving. Check OpenTrack is started and its output
  is `127.0.0.1:4242`, or that your phone app has this PC's address and that port.
- Tracking may be toggled off. Press `End` (or `Ctrl+Shift+Y`).
- The view does not move while you are aiming: that is the default ADS mode. Press
  `Insert` (or `Ctrl+Shift+U`) to cycle to one of the two that keep tracking live through
  an aim.

**Jittery or unstable tracking**

- A phone sending a raw, unfiltered pose direct to the mod will jitter. Route it through
  OpenTrack so its filters and curves clean the feed up first. See
  [Phone App Setup](#phone-app-setup).
- Raise `RemoteSmoothing` for a tracker on the network, or `LocalSmoothing` for one on
  this PC.
- A tracker on this PC sending to the machine's LAN address rather than `127.0.0.1` is
  classed as remote and gets `RemoteSmoothing`, which may be heavier than you expected.
  Point it at `127.0.0.1`.

**Wrong rotation axis: yaw feels wrong when looking up or down at extreme angles**

- Toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`).
  World-locked, the default, is horizon-stable; camera-local follows the camera's current
  up-axis.

**The stock crosshair stays in the middle when you turn your head**

- Check `ShowAimMarker=true`. With sights raised, choose the `marker` ADS mode
  to enable correction while tracking continues. Check `HeadTracking.log` for
  `[crosshair]` messages or a warning about a missing projection.
- The game controls whether its crosshair is visible. The mod moves that crosshair
  and draws no replacement when the game hides it.
- Correction currently follows the aim direction. Positional lean still introduces
  parallax, particularly at close range.

**The game window moved when you launched, or after you changed the resolution**

- By design, and only when you play windowed. BioShock Infinite opens its window
  against the top-left corner of the screen and leaves it there, so the mod centers it on
  the monitor it is on every time the game gives it a new size. It centers on the usable
  part of the screen, so a taskbar never covers the title bar. A window that already
  fills the screen is left where it is, as is one you have dragged somewhere yourself.

## Updating

Download the new release and run `install.cmd` again. It overwrites the DLL and leaves
`HeadTracking.ini` alone, so your settings and hotkeys are preserved.

## Uninstalling

Run `uninstall.cmd`. It removes the mod DLL, restores any `xinput1_3.dll.backup` it made,
and deletes `HeadTracking.ini`, `HeadTracking.log` and `HeadTracking.prev.log`. This mod
has no separate mod loader, so `uninstall.cmd /force`, which is what removes a loader the
installer did not put there, has nothing extra to take away here. Deleting
`Binaries\Win32\xinput1_3.dll` by hand does the same job.

## Building from Source

```powershell
git clone --recursive https://github.com/itsloopyo/bioshock-infinite-headtracking
cd bioshock-infinite-headtracking
pixi run package
```

Needs [pixi](https://pixi.sh) and Visual Studio with the C++ workload. The build is 32-bit
because the game is, and it does not need the game installed: everything it compiles
against is in the repo. `pixi run test` runs the unit tests, and `pixi run install`
deploys a release build straight into the game folder.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and
  new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch
  of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. Third-party components compiled into the
DLL are listed with their notices in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- BioShock Infinite (c) 2K Games / Irrational Games.
- [OpenTrack](https://github.com/opentrack/opentrack) for the UDP protocol.
- [MinHook](https://github.com/TsudaKageyu/minhook) for function hooking.

## Disclaimer

This is an unofficial, non-commercial fan modification. It is not affiliated with,
endorsed by, or sponsored by 2K Games, Take-Two Interactive, or Irrational Games. Use at
your own risk. "BioShock Infinite" and all related names, logos, and marks are trademarks
of their respective owners and are used here only to identify the game the mod applies to.

The mod contains no game code, assets, or data. It ships a single DLL of original work
that hooks the running game in memory, and it requires a legitimately purchased copy of
BioShock Infinite. It defeats no copy protection and includes no part of the game, nor of
Microsoft's XInput runtime, which it loads from your own Windows installation.

The clip at the top of this page is a short piece of BioShock Infinite gameplay footage,
copyright its respective owners, included solely to demonstrate what the mod does. No
claim of ownership is made over it. If a rights holder would prefer it removed, open an
issue and it comes down.
