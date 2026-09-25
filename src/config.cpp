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
using cameraunlock::config::LegacyInput;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// A legacy nav key and its chord switch as one key list. The frozen reader hands back a
// code from 0 to 0xFE: anything else in the file already fell back to the shipped key there,
// and 0 was the poller's unbound.
std::string LegacyKeyList(int vk, bool chord, int letter) {
    std::vector<KeyBinding> bindings;
    if (vk != 0) bindings.push_back({KeyModifiers::kNone, vk});
    if (chord) bindings.push_back({KeyModifiers::kCtrl | KeyModifiers::kShift, letter});
    return cameraunlock::input::FormatKeyBindings(bindings);
}

void MapLegacy(const legacy::Config& c, Config& out, std::vector<DroppedValue>& dropped) {
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
    // resolved that, so both are explicit here.
    out.pos_limit_x = c.pos_limit_x;
    out.pos_limit_y = c.pos_limit_y;
    out.pos_limit_y_down = c.pos_limit_y_down;
    out.pos_limit_z = c.pos_limit_z;
    out.pos_limit_z_back = c.pos_limit_z_back;

    out.toggle_key = LegacyKeyList(c.vk_toggle, c.chord_toggle, 'Y');
    out.cycle_tracking_mode_key = LegacyKeyList(c.vk_cycle_mode, c.chord_cycle_mode, 'G');
    out.yaw_mode_key = LegacyKeyList(c.vk_yaw_mode, c.chord_yaw_mode, 'H');

    out.fov_override = c.fov_override;
    out.state_probe = c.state_probe;
    out.aim_geometry_log = c.aim_geometry_log;

    // Whether the game's crosshair follows the aim is no longer a setting: it always does.
    if (!c.show_aim_marker) {
        dropped.push_back({DropRule::Reticle, "General", "ShowAimMarker", "false"});
    }
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
        MapLegacy(read, out, dropped);
        return ImportResult::Absent(std::move(dropped));
    }
    std::string path = folder;
    for (const wchar_t c : input.path.substr(slash + 1)) path.push_back(static_cast<char>(c));

    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MapLegacy(read, out, dropped);
        return ImportResult::Absent(std::move(dropped));
    }
    if (!legacy::Read(path.c_str(), read)) {
        return ImportResult::Refused(
            "[General] Port is not a number from 1024 to 65535, which the last version refused too, so "
            "head tracking stays off until the Port line is fixed");
    }
    MapLegacy(read, out, dropped);
    return ImportResult::Imported(std::move(dropped));
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

}  // namespace BioShockInfiniteHeadTracking
