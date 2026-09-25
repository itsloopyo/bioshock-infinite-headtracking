#include "legacy_config.h"

#include <cstring>

#include "legacy_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"

#include <windows.h>

#include <clocale>
#include <cstdlib>
#include <string>

namespace BioShockInfiniteHeadTracking::legacy {

namespace {

// The defaults live in config.h so the writer below, the reader's fallbacks and
// Config's own member initialisers all name the same constant.
using namespace defaults;

bool FileExists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
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

// The aim-down-sights mode cycle, retired. Head tracking now carries on through every
// aim, so there is no mode to pick and no key to cycle one. Noted rather than warned:
// an INI written by an older build carries these keys through no choice of the player's,
// and nothing they could set them to would change what happens.
void NoteRetiredAdsKeys(const cameraunlock::IniReader& reader) {
    static const struct { const char* section; const char* key; } kRetired[] = {
        { "General", "AdsMode" }, { "Hotkeys", "AdsMode" }, { "Hotkeys", "ChordAdsMode" },
    };
    for (const auto& entry : kRetired) {
        if (reader.ReadString(entry.section, entry.key, "").empty()) {
            continue;
        }
        Log::Line("Config key [%s] %s is no longer used and is ignored: head tracking stays "
                  "on while you aim, with no mode or key for it.", entry.section, entry.key);
    }
}

// Every key an existing INI may still set that the mod no longer reads, named once each
// so the user is not left adjusting a number that does nothing.
void WarnRetiredKeys(const cameraunlock::IniReader& ini) {
    WarnRetiredSmoothingKey(ini, "Smoothing", "Smoothing");
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
    WarnRetiredShapingKeys(ini);
    WarnRetiredRecenterKeys(ini);
    NoteRetiredAdsKeys(ini);
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
    cfg.chord_toggle     = ReadCheckedBool(ini, "Hotkeys", "ChordToggle",    kChord);
    cfg.chord_cycle_mode = ReadCheckedBool(ini, "Hotkeys", "ChordCycleMode", kChord);
    cfg.chord_yaw_mode   = ReadCheckedBool(ini, "Hotkeys", "ChordYawMode",   kChord);
}

void ReadDiagnosticsSection(const cameraunlock::IniReader& ini, Config& cfg) {
    cfg.state_probe = ReadCheckedBool(ini, "Diagnostics", "StateProbe", kStateProbe);
    cfg.aim_geometry_log = ReadCheckedBool(ini, "Diagnostics", "AimGeometry", kAimGeometryLog);
}

}  // namespace

bool Read(const char* iniPath, Config& cfg) {
    // LoadOrCreate wrote the default file here and then read it. Reading that file gives
    // the defaults cfg already holds, so there is nothing to read and nothing is written.
    if (!FileExists(iniPath)) {
        return true;
    }

    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        Log::Line("ERROR: Failed to open INI: %s", iniPath);
        return false;
    }

    if (!ReadGeneralSection(ini, cfg)) {
        return false;
    }
    ReadViewSection(ini, cfg);
    ReadSmoothingSection(ini, cfg);
    WarnRetiredKeys(ini);
    ReadPositionSection(ini, cfg);
    ReadHotkeysSection(ini, cfg);
    ReadDiagnosticsSection(ini, cfg);

    return true;
}

}  // namespace BioShockInfiniteHeadTracking::legacy
