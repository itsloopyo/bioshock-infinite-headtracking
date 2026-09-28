#include "config.h"

#include "legacy_config/legacy_config.h"
#include "path_utils.h"

#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace BioShockInfiniteHeadTracking {

namespace {

using cameraunlock::config::DropRule;
using cameraunlock::config::DroppedValue;
using cameraunlock::config::ImportResult;
using cameraunlock::config::LegacyClampToRange;
using cameraunlock::config::LegacyInput;
using cameraunlock::config::schema::Concept;
using cameraunlock::input::KeyModifiers;

// A legacy nav key and its chord switch as one key list. The frozen reader hands back a
// code from 0 to 0xFE: anything else in the file already fell back to the shipped key there,
// and 0 was the poller's unbound. A Ctrl, Shift or Alt key on its own imports as unbound and
// is logged (N3), and the chord still follows it.
std::string LegacyKeyList(int vk, const char* key, bool chord, int letter, std::vector<DroppedValue>& dropped) {
    std::string list = cameraunlock::config::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    if (chord) {
        const std::string chord_list =
            cameraunlock::input::FormatKeyBindings({{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
        list += (list.empty() ? "" : ", ") + chord_list;
    }
    return list;
}

// Maps the frozen reader's settings, and returns the rows the player never changed from what
// the last pre-canonical build shipped, which follow Defaults.ini.
std::vector<Concept> MapLegacy(const legacy::Config& c, Config& out, std::vector<DroppedValue>& dropped) {
    out.udp_port = c.udp_port;
    out.enable_on_startup = c.enabled_on_startup;
    out.world_space_yaw = c.world_space_yaw;
    out.data_freshness_ms = c.data_freshness_ms;
    out.local_smoothing = c.local_smoothing;
    out.remote_smoothing = c.remote_smoothing;

    // [Position] Enabled picked the mode the session started in, and the cycle still
    // reached every mode, so it is the startup pair and nothing more.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                           : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    // LimitYDown fell back to LimitY when the file left it out; the frozen reader already
    // resolved that, so both are explicit here. The reader took any finite limit from 0 up, and
    // one above the rows' 10 imports as 10 (N4).
    out.pos_limit_x = LegacyClampToRange<Concept::PositionLimitX>(c.pos_limit_x, "Position", "LimitX", dropped);
    out.pos_limit_y = LegacyClampToRange<Concept::PositionLimitY>(c.pos_limit_y, "Position", "LimitY", dropped);
    out.pos_limit_y_down =
        LegacyClampToRange<Concept::PositionLimitYDown>(c.pos_limit_y_down, "Position", "LimitYDown", dropped);
    out.pos_limit_z = LegacyClampToRange<Concept::PositionLimitZ>(c.pos_limit_z, "Position", "LimitZ", dropped);
    out.pos_limit_z_back =
        LegacyClampToRange<Concept::PositionLimitZBack>(c.pos_limit_z_back, "Position", "LimitZBack", dropped);

    out.toggle_key = LegacyKeyList(c.vk_toggle, "Toggle", c.chord_toggle, 'Y', dropped);
    out.cycle_tracking_mode_key = LegacyKeyList(c.vk_cycle_mode, "CycleMode", c.chord_cycle_mode, 'G', dropped);
    out.yaw_mode_key = LegacyKeyList(c.vk_yaw_mode, "YawMode", c.chord_yaw_mode, 'H', dropped);

    out.fov_override = c.fov_override;
    out.state_probe = c.state_probe;
    out.aim_geometry_log = c.aim_geometry_log;

    // Whether the game's crosshair follows the aim is no longer a setting: it always does.
    if (!c.show_aim_marker) {
        dropped.push_back({DropRule::Reticle, "General", "ShowAimMarker", "false"});
    }

    // Each hotkey row is its nav key and its chord switch together. A limit is compared as
    // read, so one N4 clamped is the player's.
    const legacy::Config shipped;
    cameraunlock::config::LegacyFollowsDefaultsIni follows;
    follows.Setting(Concept::UdpPort, c.udp_port, shipped.udp_port);
    follows.Setting(Concept::EnableOnStartup, c.enabled_on_startup, shipped.enabled_on_startup);
    follows.Setting(Concept::WorldSpaceYaw, c.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(c.position_enabled, shipped.position_enabled);
    follows.Setting(Concept::DataFreshnessMs, c.data_freshness_ms, shipped.data_freshness_ms);
    follows.Setting(Concept::LocalSmoothing, c.local_smoothing, shipped.local_smoothing);
    follows.Setting(Concept::RemoteSmoothing, c.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(Concept::PositionLimitX, c.pos_limit_x, shipped.pos_limit_x);
    follows.Setting(Concept::PositionLimitY, c.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(Concept::PositionLimitYDown, c.pos_limit_y_down, shipped.pos_limit_y_down);
    follows.Setting(Concept::PositionLimitZ, c.pos_limit_z, shipped.pos_limit_z);
    follows.Setting(Concept::PositionLimitZBack, c.pos_limit_z_back, shipped.pos_limit_z_back);
    follows.Setting(Concept::ToggleKey, c.vk_toggle == shipped.vk_toggle && c.chord_toggle == shipped.chord_toggle);
    follows.Setting(Concept::CycleTrackingModeKey,
                    c.vk_cycle_mode == shipped.vk_cycle_mode && c.chord_cycle_mode == shipped.chord_cycle_mode);
    follows.Setting(Concept::YawModeKey,
                    c.vk_yaw_mode == shipped.vk_yaw_mode && c.chord_yaw_mode == shipped.chord_yaw_mode);
    return follows.Concepts();
}

ImportResult RunLegacyImport(const LegacyInput& input, Config& out) {
    // The path the published build handed its INI reader: the folder in the ANSI code
    // page, or its 8.3 alias when the code page cannot spell it. With neither, that build
    // never read a file there - it stayed dormant before looking - so there is no setting
    // of the player's to carry.
    const std::size_t slash = input.path.find_last_of(L"\\/");
    const std::string folder = AnsiFolderPath(input.path.substr(0, slash + 1));
    std::vector<DroppedValue> dropped;
    legacy::Config read;
    if (folder.empty()) {
        std::vector<Concept> follows = MapLegacy(read, out, dropped);
        return ImportResult::Absent(std::move(dropped), {}, std::move(follows));
    }
    std::string path = folder;
    for (const wchar_t c : input.path.substr(slash + 1)) path.push_back(static_cast<char>(c));

    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::vector<Concept> follows = MapLegacy(read, out, dropped);
        return ImportResult::Absent(std::move(dropped), {}, std::move(follows));
    }
    if (!legacy::Read(path.c_str(), read)) {
        return ImportResult::Refused(
            "[General] Port is not a number from 1024 to 65535, which the last version refused too, so "
            "head tracking stays off until the Port line is fixed");
    }
    std::vector<Concept> follows = MapLegacy(read, out, dropped);
    return ImportResult::Imported(std::move(dropped), {}, std::move(follows));
}

}  // namespace

cameraunlock::config::CodecParseResult<float> FovCodec::Parse(std::string_view text) const {
    cameraunlock::config::CodecParseResult<float> read = angle_.Parse(text);
    if (read.ok() && read.value != 0.0f && read.value < kMin) {
        return {0.0f, "0, or an angle from 30 to 150"};
    }
    if (!read.ok()) read.error = "0, or an angle from 30 to 150";
    return read;
}

std::string FovCodec::Render(float value) const {
    if (value != 0.0f && value < kMin) {
        throw std::invalid_argument("[View] Fov " + std::to_string(value) + " is neither 0 nor 30 to 150");
    }
    return angle_.Render(value);
}

cameraunlock::config::ConfigTable<Config> ConfigTable() {
    using cameraunlock::config::BoolCodec;
    using C = cameraunlock::config::schema::Concept;
    cameraunlock::config::ConfigTable<Config> table{Config{}};
    table.Concept<C::UdpPort>(&Config::udp_port)
        .Concept<C::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<C::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<C::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<C::DataFreshnessMs>(&Config::data_freshness_ms)
        .Concept<C::LocalSmoothing>(&Config::local_smoothing)
        .Concept<C::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<C::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<C::PositionLimitX>(&Config::pos_limit_x)
        .Concept<C::PositionLimitY>(&Config::pos_limit_y)
        .Concept<C::PositionLimitYDown>(&Config::pos_limit_y_down)
        .Concept<C::PositionLimitZ>(&Config::pos_limit_z)
        .Concept<C::PositionLimitZBack>(&Config::pos_limit_z_back)
        .Concept<C::ToggleKey>(&Config::toggle_key)
        .Concept<C::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<C::YawModeKey>(&Config::yaw_mode_key)
        .Local("View", "Fov", &Config::fov_override, FovCodec{},
               "Field of view in degrees for a frame the game is not zooming, or 0 for the game's own.\n"
               "0, or 30 to 150. It is the horizontal angle on a 16:9 display. Iron sights, scopes and\n"
               "scripted cameras still zoom by the factor they always did, and shots, traces and aim\n"
               "assist keep the game's own angle.")
        .Local("Diagnostics", "StateProbe", &Config::state_probe, BoolCodec{},
               "true: write the player controller to HeadTracking.log every two seconds. It buries\n"
               "everything else in the log; leave it false unless you were asked to turn it on.")
        .Local("Diagnostics", "AimGeometry", &Config::aim_geometry_log, BoolCodec{},
               "true: write the head pose, the aim and where the crosshair was placed to\n"
               "HeadTracking.log every two seconds, for a report of a misplaced crosshair.");
    return table;
}

cameraunlock::config::RenderHeader ConfigHeader() {
    cameraunlock::config::RenderHeader header;
    header.display_name = "BioShock Infinite";
    return header;
}

cameraunlock::config::LegacyImport<Config> ConfigLegacyImport() {
    cameraunlock::config::LegacyImport<Config> import;
    import.run = &RunLegacyImport;
    // Every key the frozen reader takes a value from. The retired keys it only names in
    // the log (the old sensitivity, smoothing, recentre and ADS keys) are not here: no
    // value of theirs reached anything, and the conversion logs each as not carried.
    import.keys = {
        {"General", "EnableOnStartup"}, {"General", "Port"},
        {"General", "DataFreshnessMs"}, {"General", "WorldSpaceYaw"},
        {"General", "ShowAimMarker"},   {"View", "Fov"},
        {"Smoothing", "LocalSmoothing"}, {"Smoothing", "RemoteSmoothing"},
        {"Position", "Enabled"},        {"Position", "LimitX"},
        {"Position", "LimitY"},         {"Position", "LimitYDown"},
        {"Position", "LimitZ"},         {"Position", "LimitZBack"},
        {"Hotkeys", "Toggle"},          {"Hotkeys", "CycleMode"},
        {"Hotkeys", "YawMode"},         {"Hotkeys", "ChordToggle"},
        {"Hotkeys", "ChordCycleMode"},  {"Hotkeys", "ChordYawMode"},
        {"Diagnostics", "StateProbe"},  {"Diagnostics", "AimGeometry"},
    };
    return import;
}

cameraunlock::config::ConfigOwnerOptions<Config> ConfigOptions(const std::wstring& folder,
                                                               cameraunlock::config::DefaultsFile defaults) {
    cameraunlock::config::ConfigOwnerOptions<Config> options;
    options.path = folder + L"CameraUnlock.ini";
    options.legacy_path = folder + L"HeadTracking.ini";
    options.table = ConfigTable();
    options.import = ConfigLegacyImport();
    options.header = ConfigHeader();
    options.defaults = std::move(defaults);
    return options;
}

}  // namespace BioShockInfiniteHeadTracking
