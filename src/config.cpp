#include "config.h"

#include "logging.h"
#include "legacy_config/legacy_config.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

namespace BioShockInfiniteHeadTracking {

namespace {

// The defaults live in config.h so the writer below, the reader's fallbacks and
// Config's own member initialisers all name the same constant.
using namespace defaults;

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

void WriteGeneralSection(cameraunlock::IniWriter& w) {
    w.WriteSection("General");
    w.WriteBool("EnableOnStartup", kEnableOnStartup);
    w.WriteInt("Port", kPort);
    w.WriteInt("DataFreshnessMs", kDataFreshnessMs);
    w.WriteComment(" Yaw mode: true = horizon-locked yaw (default), false = camera-local.");
    w.WriteBool("WorldSpaceYaw", kWorldSpaceYaw);
    w.WriteComment(" Move the stock crosshair to follow the weapon's aim while your head turns.");
    w.WriteComment(" No extra reticle is drawn. False leaves the stock position unchanged.");
    w.WriteBool("ShowAimMarker", kShowAimMarker);
    w.WriteBlankLine();
}

void WriteViewSection(cameraunlock::IniWriter& w) {
    w.WriteSection("View");
    w.WriteComment(" Field of view in degrees, or 0 to render the game's own. Valid");
    w.WriteComment(" values are 30 to 150. The game's own Field of View slider is an");
    w.WriteComment(" offset its config file caps at 15 percent of the angle already");
    w.WriteComment(" being rendered, so anything wider than that comes from here.");
    w.WriteComment(" It is applied as a ratio against the angle the camera renders at");
    w.WriteComment(" when nothing is zooming, so an unzoomed frame is drawn at exactly");
    w.WriteComment(" this number and iron sights, scopes and scripted cameras still zoom");
    w.WriteComment(" by the same factor they always did. Only the rendered frame is");
    w.WriteComment(" widened: shots, traces and aim assist keep the game's own angle.");
    w.WriteComment(" HeadTracking.log names the angles each frame was projected with.");
    w.WriteDouble("Fov", kFovOverride);
    w.WriteBlankLine();
}

void WriteSmoothingSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Smoothing");
    w.WriteComment(" Smoothing 0.0 (responsive) - 1.0 (heavy). Covers rotation and position.");
    w.WriteComment(" The value is picked per connection from the packet source address:");
    w.WriteComment(" LocalSmoothing for a tracker running on this PC (loopback),");
    w.WriteComment(" RemoteSmoothing for a phone or other device on the network.");
    w.WriteDouble("LocalSmoothing", kLocalSmoothing);
    w.WriteDouble("RemoteSmoothing", kRemoteSmoothing);
    w.WriteBlankLine();
}

void WritePositionSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Position");
    w.WriteComment(" 6DOF positional tracking. The pose is used at 1:1 - shape it in your tracker,");
    w.WriteComment(" not here. The limits below are metres of head travel, not a sensitivity.");
    w.WriteBool("Enabled", kPositionEnabled);
    w.WriteDouble("LimitX", kPosLimitX);
    w.WriteComment(" Vertical travel is clamped to [-LimitYDown, +LimitY]: how far the view");
    w.WriteComment(" may rise and how far it may drop, as separate metre budgets.");
    w.WriteDouble("LimitY", kPosLimitY);
    w.WriteDouble("LimitYDown", kPosLimitYDown);
    w.WriteDouble("LimitZ", kPosLimitZ);
    w.WriteDouble("LimitZBack", kPosLimitZBack);
    w.WriteBlankLine();
}

void WriteHotkeysSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Hotkeys");
    w.WriteComment(" Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode).");
    w.WriteHex("Toggle", kVkToggle);
    w.WriteHex("CycleMode", kVkCycleMode);
    w.WriteHex("YawMode", kVkYawMode);
    w.WriteComment(" Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode).");
    w.WriteBool("ChordToggle", kChord);
    w.WriteBool("ChordCycleMode", kChord);
    w.WriteBool("ChordYawMode", kChord);
    w.WriteBlankLine();
}

void WriteDiagnosticsSection(cameraunlock::IniWriter& w) {
    w.WriteSection("Diagnostics");
    w.WriteComment(" Dump the player controller to HeadTracking.log every two seconds, as");
    w.WriteComment(" rows of hex dwords. A maintenance diagnostic: it writes several");
    w.WriteComment(" kilobytes a second and will bury everything else in the log. Leave it");
    w.WriteComment(" false unless you are diffing the dumps to find where a new game build");
    w.WriteComment(" moved a field.");
    w.WriteBool("StateProbe", kStateProbe);
    w.WriteBlankLine();
    w.WriteComment(" Sample the head pose, the aim resolved in the tracked view and the");
    w.WriteComment(" screen position it projected to, once every two seconds. Turn it on");
    w.WriteComment(" when the crosshair sits in the wrong place and you are reporting it;");
    w.WriteComment(" a session of it is thousands of lines, and the startup, tracking and");
    w.WriteComment(" crosshair lines you would otherwise be reading sit between them.");
    w.WriteBool("AimGeometry", kAimGeometryLog);
}

// Returns false when the file could not be created, so the caller reports the real
// reason. Failing silently here surfaces one step later as "Failed to open INI",
// which reads as a corrupt file rather than a directory the game cannot write to.
bool WriteDefaultIni(const char* path) {
    cameraunlock::IniWriter w;
    if (!w.Open(path)) {
        Log::Line("ERROR: could not create %s (error %lu). The mod cannot store its "
                  "settings; check that the game directory is writable.",
                  path, GetLastError());
        return false;
    }
    w.WriteComment(" BioShock Infinite - Head Tracking configuration");
    w.WriteComment(" Lives next to BioShockInfinite.exe in Binaries/Win32/.");
    w.WriteBlankLine();
    WriteGeneralSection(w);
    WriteViewSection(w);
    WriteSmoothingSection(w);
    WritePositionSection(w);
    WriteHotkeysSection(w);
    WriteDiagnosticsSection(w);
    w.Close();
    return true;
}

}  // namespace

bool Config::LoadOrCreate(const char* iniPath) {
    if (!FileExists(iniPath) && !WriteDefaultIni(iniPath)) {
        return false;
    }

    legacy::Config read;
    if (!legacy::Read(iniPath, read)) {
        return false;
    }
    enabled_on_startup = read.enabled_on_startup;
    udp_port = read.udp_port;
    local_smoothing = read.local_smoothing;
    remote_smoothing = read.remote_smoothing;
    show_aim_marker = read.show_aim_marker;
    data_freshness_ms = read.data_freshness_ms;
    world_space_yaw = read.world_space_yaw;
    fov_override = read.fov_override;
    state_probe = read.state_probe;
    aim_geometry_log = read.aim_geometry_log;
    position_enabled = read.position_enabled;
    pos_limit_x = read.pos_limit_x;
    pos_limit_y = read.pos_limit_y;
    pos_limit_y_down = read.pos_limit_y_down;
    pos_limit_z = read.pos_limit_z;
    pos_limit_z_back = read.pos_limit_z_back;
    vk_toggle = read.vk_toggle;
    vk_cycle_mode = read.vk_cycle_mode;
    vk_yaw_mode = read.vk_yaw_mode;
    chord_toggle = read.chord_toggle;
    chord_cycle_mode = read.chord_cycle_mode;
    chord_yaw_mode = read.chord_yaw_mode;
    return true;
}

}  // namespace BioShockInfiniteHeadTracking
