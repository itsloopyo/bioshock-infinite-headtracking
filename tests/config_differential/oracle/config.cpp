#include "config.h"

#include <cstring>

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

#include <clocale>
#include <cstdlib>
#include <string>

namespace BioShockInfiniteHeadTracking {

namespace {

// The defaults live in config.h so the writer below, the reader's fallbacks and
// Config's own member initialisers all name the same constant.
using namespace defaults;

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

// The path the last LoadOrCreate used. SaveAdsMode writes one key back into that same
// file rather than deriving a path of its own: the caller owns where the INI lives, and
// two answers to that question is one file the player edits and another the mod writes.
// Never destroyed, for the reason set out over the globals in dllmain.cpp: a
// namespace-scope std::string frees its heap in the CRT's DLL_PROCESS_DETACH pass, under
// the loader lock, after ExitProcess has already terminated this mod's threads wherever
// they happened to be - including inside the heap lock.
std::string& IniPathStore() {
    static std::string* const path = new std::string();
    return *path;
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
    w.WriteComment(" What head tracking does while the sights are up. Cycled in game with");
    w.WriteComment(" Insert (or Ctrl+Shift+U), which writes the new value back here.");
    w.WriteComment("   paused   the game keeps the camera for as long as the sights are up");
    w.WriteComment("   marker   tracking carries on with stock crosshair position correction");
    w.WriteComment("   tracked  tracking carries on with the stock crosshair position unchanged");
    w.WriteString("AdsMode", AdsModeValue(kAdsMode));
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
    w.WriteComment(" Virtual-key codes. Defaults: End (toggle), Page Up (cycle tracking mode), Page Down (yaw mode), Insert (cycle ADS mode).");
    w.WriteHex("Toggle", kVkToggle);
    w.WriteHex("CycleMode", kVkCycleMode);
    w.WriteHex("YawMode", kVkYawMode);
    w.WriteHex("AdsMode", kVkAdsMode);
    w.WriteComment(" Chord alternatives: Ctrl+Shift+Y (toggle), Ctrl+Shift+G (cycle tracking mode), Ctrl+Shift+H (yaw mode), Ctrl+Shift+U (cycle ADS mode).");
    w.WriteBool("ChordToggle", kChord);
    w.WriteBool("ChordCycleMode", kChord);
    w.WriteBool("ChordYawMode", kChord);
    w.WriteBool("ChordAdsMode", kChord);
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
    w.WriteComment(" when the aim marker sits in the wrong place and you are reporting it;");
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

// Reads a float and runs it through the boundary check for its key, warning when the
// file held something the mod will not use. One place where the read, the check and the
// report meet, so a key cannot end up reported under another key's name or written into
// the struct without having been checked at all.
// Parsed in the "C" locale, pinned, exactly as cameraunlock-core's reader pins it.
//
// A plain strtod follows LC_NUMERIC, and a game that calls setlocale(LC_ALL, "") on a
// German install then makes `0,4` a COMPLETE parse here while the value reader - which is
// pinned - still returns 0.0. The check would pass, nothing would be written to the log,
// and the silent zero it exists to catch would be back with an extra step in front of it.
double ParseDouble(const char* text, char** end) {
#ifdef _MSC_VER
    static const _locale_t kC = _create_locale(LC_NUMERIC, "C");
    return _strtod_l(text, end, kC);
#else
    return std::strtod(text, end);
#endif
}

// The key's text with any trailing comment removed, then trimmed.
//
// GetPrivateProfileString hands back everything after the '=', comment and all, and
// core's numeric readers absorb that by parsing a PREFIX and stopping. So `0.4 ; deeper
// lean` is a documented-safe value that reads as 0.4, and a whole-token check that did
// not strip the comment first would reject it and quietly substitute the default - the
// same silent reset this check exists to prevent, aimed at a different user.
std::string WithoutComment(const std::string& text) {
    const std::size_t cut = text.find_first_of(";#");
    const std::string body = (cut == std::string::npos) ? text : text.substr(0, cut);
    const std::size_t first = body.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    const std::size_t last = body.find_last_not_of(" \t\r\n");
    return body.substr(first, last - first + 1);
}

// Whether the whole of a key's value parsed as a number, not just a prefix of it.
//
// The reader stops at the first character it cannot use and reports what it got up to
// there, so `0,4` parses as `0`. A comma is the decimal separator on most of Europe's
// desktops and is exactly what a player hand-editing the file types; `LimitZ=0,4` then
// sanitises to a legal 0.0 and forward lean is silently dead for the session, because the
// value the range check saw was already the value it would have chosen.
bool ParsesWhole(const std::string& text) {
    const std::string value = WithoutComment(text);
    if (value.empty()) {
        return true;  // absent or blank: the default applies, which is not an error
    }
    char* end = nullptr;
    ParseDouble(value.c_str(), &end);
    return end != nullptr && *end == '\0';
}

template <typename Sanitizer>
float ReadSanitized(const cameraunlock::IniReader& ini, const char* section,
                    const char* key, float fallback, Sanitizer clean) {
    const std::string text = ini.ReadString(section, key, "");
    if (!ParsesWhole(text)) {
        Log::Line("WARN: INI %s.%s value '%s' is not a number - a decimal comma is the "
                  "usual cause, this file wants a point. Using %.4f",
                  section, key, text.c_str(), fallback);
        return fallback;
    }
    const float raw = ini.ReadFloat(section, key, fallback);
    const float value = clean(raw);
    if (raw != value) {
        Log::Line("WARN: INI %s.%s value %.4f out of range or non-finite; using %.4f",
                  section, key, raw, value);
    }
    return value;
}

// A yes/no setting, decided here rather than by the core reader.
//
// Two reasons not to delegate. Core matches the WHOLE string against a fixed table of
// exact spellings, so it silently defaults on `0 ; no lean` - a trailing comment, which it
// documents as safe for numbers - and on any casing outside the three it lists, so `tRue`
// reads as the default. And the fallback for a bool can be the OPPOSITE of what the player
// typed: someone who has just switched positional tracking off gets it back on, with
// nothing in the log. Anything genuinely unrecognised warns and keeps the default.
bool ReadCheckedBool(const cameraunlock::IniReader& ini, const char* section,
                     const char* key, bool fallback) {
    const std::string value = WithoutComment(ini.ReadString(section, key, ""));
    if (value.empty()) {
        return fallback;
    }
    static const char* const kTrue[] = { "1", "true", "yes", "on" };
    static const char* const kFalse[] = { "0", "false", "no", "off" };
    for (const char* accepted : kTrue) {
        if (_stricmp(value.c_str(), accepted) == 0) {
            return true;
        }
    }
    for (const char* accepted : kFalse) {
        if (_stricmp(value.c_str(), accepted) == 0) {
            return false;
        }
    }
    Log::Line("WARN: INI %s.%s value '%s' is not a yes/no setting (use true or false); "
              "using %s", section, key, value.c_str(), fallback ? "true" : "false");
    return fallback;
}

// The four [Position] limits share one boundary check, whose NaN fallback is the key's
// own shipped default, so each of them is one line at the call site.
float ReadPositionLimit(const cameraunlock::IniReader& ini, const char* key,
                        float fallback) {
    return ReadSanitized(ini, "Position", key, fallback,
                         [fallback](float v) { return SanitizePositionLimit(v, fallback); });
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    if (reader.ReadString(section, key, "").empty()) return;
    Log::Line(
        "WARN: Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

// The pose-shaping keys, retired together. The tracker owns pose shaping: sensitivity,
// deadzone and axis inversion are configured once in OpenTrack or the phone app, where
// one profile then behaves the same in every game, instead of per-game here. Silently
// ignoring a key an existing INI still sets would leave the user adjusting a number that
// does nothing, so EVERY key still present is named - not just the first one found. A
// user who set three of them and is told about one goes back to the file, deletes that
// one, and is no better off.
//
// Protocol-to-engine sign conversion is NOT one of these and stays in the camera hook,
// where it belongs: that is a fact about UE3, not a preference. Nor is PositionScale's
// job of turning metres into the engine's centimetres, which is now a constant
// (ue3_types.h) rather than a key.
void WarnRetiredShapingKeys(const cameraunlock::IniReader& reader) {
    static const struct { const char* section; const char* key; } kRetired[] = {
        { "Sensitivity", "Yaw" },         { "Sensitivity", "Pitch" },
        { "Sensitivity", "Roll" },        { "Sensitivity", "InvertYaw" },
        { "Sensitivity", "InvertPitch" }, { "Sensitivity", "InvertRoll" },
        { "Smoothing",   "DeadzoneDeg" }, { "Position",    "SensitivityX" },
        { "Position",    "SensitivityY" },{ "Position",    "SensitivityZ" },
        { "Position",    "InvertX" },     { "Position",    "InvertY" },
        { "Position",    "InvertZ" },     { "Position",    "PositionScale" },
    };
    for (const auto& entry : kRetired) {
        if (reader.ReadString(entry.section, entry.key, "").empty()) {
            continue;
        }
        Log::Line("WARN: Config key [%s] %s has been retired and is IGNORED. The mod uses "
                  "the tracker's pose at 1:1 so one tracker profile behaves the same in "
                  "every game - set sensitivity, deadzones and axis inversion in "
                  "OpenTrack or your phone app instead.", entry.section, entry.key);
    }
}

// The recentre binding, retired earlier. The mod keeps no centre of its own: it applies
// whatever pose the tracker sends, the way a TrackIR driver's game support does. Two
// centres in series drift apart, because each side recentres at moments the other cannot
// see, and the player then needs two presses for one recentre. An INI that still binds a
// key here would silently bind nothing.
void WarnRetiredRecenterKeys(const cameraunlock::IniReader& reader) {
    for (const char* key : { "Recenter", "ChordRecenter" }) {
        if (reader.ReadString("Hotkeys", key, "").empty()) {
            continue;
        }
        Log::Line("WARN: Config key [Hotkeys] %s has been retired and is IGNORED. The mod "
                  "keeps no centre of its own, so there is nothing for it to bind - "
                  "recentre in your tracker instead (opentrack's Center bind, or the "
                  "CENTER button in a phone app).", key);
    }
}

// Every key an existing INI may still set that the mod no longer reads, named once each
// so the user is not left adjusting a number that does nothing.
void WarnRetiredKeys(const cameraunlock::IniReader& ini) {
    WarnRetiredSmoothingKey(ini, "Smoothing", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
    WarnRetiredShapingKeys(ini);
    WarnRetiredRecenterKeys(ini);
}

// GetAsyncKeyState, which the poller polls these with, is defined for virtual-key codes
// 0x01-0xFE; 0 is the poller's own "this hotkey is unbound" sentinel. Anything else is a
// typo (an extra digit, a scan code pasted in place of a VK) that reaches the poller,
// polls nothing, and leaves the user with a key that silently never fires. Report it and
// keep the shipped binding rather than shipping a dead one.
constexpr int kMaxVirtualKey = 0xFE;

int ReadVirtualKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const int vk = ini.ReadHex("Hotkeys", key, fallback);
    if (vk < 0 || vk > kMaxVirtualKey) {
        Log::Line("WARN: INI Hotkeys.%s value 0x%X is not a virtual-key code (0x01-0xFE, "
                  "or 0 to unbind); using 0x%02X", key, vk, fallback);
        return fallback;
    }
    return vk;
}

// The INI is read section by section, in the order the file is written, so a key added to
// one has exactly one place to be read from and one place to be written.
bool ReadGeneralSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.enabled_on_startup = ReadCheckedBool(ini, "General", "EnableOnStartup", kEnableOnStartup);
    // The raw text first. The integer reader answers 0 for anything it cannot parse
    // rather than handing back the default, so `Port=default` used to abort with
    // "port 0 out of range" - a number the player never typed, on the one path in this
    // file that is fatal and therefore the one whose message has to be right.
    //
    // A trailing comment and a leading sign are both legal: core documents the first as
    // safe on an unquoted number, and rejecting either here would turn a working config
    // into total dormancy, which is a far worse outcome than the message it was fixing.
    const std::string portText = WithoutComment(ini.ReadString("General", "Port", ""));
    if (!portText.empty()) {
        char* end = nullptr;
        std::strtol(portText.c_str(), &end, 10);
        if (end == portText.c_str() || *end != '\0') {
            Log::Line("ERROR: INI General.Port value '%s' is not a number. Expected %d-%d.",
                      portText.c_str(), kMinPort, kMaxPort);
            return false;
        }
    }
    int port = ini.ReadInt("General", "Port", kPort);
    if (port < kMinPort || port > kMaxPort) {
        Log::Line("ERROR: INI General.Port %d out of range %d-%d", port, kMinPort, kMaxPort);
        return false;
    }
    cfg.udp_port = static_cast<uint16_t>(port);
    cfg.data_freshness_ms = ini.ReadInt("General", "DataFreshnessMs", kDataFreshnessMs);
    if (cfg.data_freshness_ms <= 0) {
        Log::Line("WARN: INI General.DataFreshnessMs %d is not a positive window; using %d",
                  cfg.data_freshness_ms, kDataFreshnessMs);
        cfg.data_freshness_ms = kDataFreshnessMs;
    }
    cfg.world_space_yaw = ReadCheckedBool(ini, "General", "WorldSpaceYaw", kWorldSpaceYaw);
    cfg.show_aim_marker = ReadCheckedBool(ini, "General", "ShowAimMarker", kShowAimMarker);
    // Anything that is not one of the three values is the DEFAULT rather than whichever
    // branch happens to be last, which covers a typo in a hand-edited file and is also
    // the migration path for a mode renamed since an older release wrote the key.
    cfg.ads_mode = ParseAdsMode(
        ini.ReadString("General", "AdsMode", AdsModeValue(kAdsMode)).c_str());
    return true;
}

void ReadViewSection(const cameraunlock::IniReader& ini, Config& cfg) {
    // Zero is the off switch and passes through; a negative number is a typo for it
    // rather than an angle, so it lands there too. Anything else is clamped into the
    // range that has a projection, which is what someone typing 200 is asking for.
    cfg.fov_override = ReadSanitized(ini, "View", "Fov", kFovOverride, [](float v) {
        const float f = SanitizeFinite(v, kFovOverride);
        return f <= 0.0f ? kFovOverride : ClampRange(f, kMinFovOverride, kMaxFovOverride);
    });
}

void ReadSmoothingSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.local_smoothing = ReadSanitized(
        ini, "Smoothing", "LocalSmoothing", kLocalSmoothing,
        [](float v) { return SanitizeSmoothing(v, kLocalSmoothing); });
    cfg.remote_smoothing = ReadSanitized(
        ini, "Smoothing", "RemoteSmoothing", kRemoteSmoothing,
        [](float v) { return SanitizeSmoothing(v, kRemoteSmoothing); });
}

void ReadPositionSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.position_enabled = ReadCheckedBool(ini, "Position", "Enabled", kPositionEnabled);
    // Checked on the same terms as the rotation values above: these feed the position
    // processor and are then added straight to the view location, so one "LimitZ=nan"
    // puts a NaN in the camera's world position and the frame renders black.
    cfg.pos_limit_x = ReadPositionLimit(ini, "LimitX", kPosLimitX);
    cfg.pos_limit_y = ReadPositionLimit(ini, "LimitY", kPosLimitY);
    // Falls back to whatever LimitY resolved to, not to kPosLimitYDown: a config that
    // sets only LimitY would otherwise keep 0.20 m of downward travel while the upward
    // budget moved, with nothing in the log saying the key was half-effective.
    cfg.pos_limit_y_down = ReadPositionLimit(ini, "LimitYDown", cfg.pos_limit_y);
    cfg.pos_limit_z = ReadPositionLimit(ini, "LimitZ", kPosLimitZ);
    cfg.pos_limit_z_back = ReadPositionLimit(ini, "LimitZBack", kPosLimitZBack);
}

void ReadHotkeysSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.vk_toggle     = ReadVirtualKey(ini, "Toggle",    kVkToggle);
    cfg.vk_cycle_mode = ReadVirtualKey(ini, "CycleMode", kVkCycleMode);
    cfg.vk_yaw_mode   = ReadVirtualKey(ini, "YawMode",   kVkYawMode);
    cfg.vk_ads_mode   = ReadVirtualKey(ini, "AdsMode",   kVkAdsMode);
    cfg.chord_toggle     = ReadCheckedBool(ini, "Hotkeys", "ChordToggle",    kChord);
    cfg.chord_cycle_mode = ReadCheckedBool(ini, "Hotkeys", "ChordCycleMode", kChord);
    cfg.chord_yaw_mode   = ReadCheckedBool(ini, "Hotkeys", "ChordYawMode",   kChord);
    cfg.chord_ads_mode   = ReadCheckedBool(ini, "Hotkeys", "ChordAdsMode",   kChord);
}

void ReadDiagnosticsSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.state_probe = ReadCheckedBool(ini, "Diagnostics", "StateProbe", kStateProbe);
    cfg.aim_geometry_log = ReadCheckedBool(ini, "Diagnostics", "AimGeometry", kAimGeometryLog);
}

}  // namespace

void Config::SaveAdsMode(AdsMode mode) {
    if (IniPathStore().empty()) {
        Log::Line("WARN: the ADS mode was cycled before any INI was loaded, so it applies "
                  "for this session but will not survive a restart");
        return;
    }
    // GetPrivateProfileString's writer half, which is the only one that can change one
    // key of an existing file. IniWriter truncates, so writing this back through it
    // would throw away every other setting and every comment in the file.
    if (!WritePrivateProfileStringA("General", "AdsMode", AdsModeValue(mode),
                                    IniPathStore().c_str())) {
        Log::Line("WARN: could not save AdsMode to %s (error %lu); the mode applies for "
                  "this session but will not survive a restart",
                  IniPathStore().c_str(), GetLastError());
    }
}

bool Config::LoadOrCreate(const char* iniPath) {
    IniPathStore() = iniPath;
    if (!FileExists(iniPath) && !WriteDefaultIni(iniPath)) {
        return false;
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }

    if (!ReadGeneralSection(ini, *this)) {
        return false;
    }
    ReadViewSection(ini, *this);
    ReadSmoothingSection(ini, *this);
    WarnRetiredKeys(ini);
    ReadPositionSection(ini, *this);
    ReadHotkeysSection(ini, *this);
    ReadDiagnosticsSection(ini, *this);

    return true;
}

}  // namespace BioShockInfiniteHeadTracking
