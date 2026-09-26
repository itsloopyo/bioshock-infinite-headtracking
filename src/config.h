#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace BioShockInfiniteHeadTracking {

// The settings, as CameraUnlock.ini holds them. The member initialisers are the defaults:
// the table below renders them into the file the mod creates at first launch, and a key
// missing from the file reads as its member's initialiser.
struct Config {
    std::uint16_t udp_port = 4242;
    bool enable_on_startup = true;
    // true = horizon-locked (world-space) yaw, false = camera-local yaw.
    bool world_space_yaw = true;
    int data_freshness_ms = 500;

    // Picked per connection from the packet source address: a tracker on this machine
    // (loopback) uses local_smoothing, anything else remote_smoothing. Both cover rotation
    // and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // The tracking mode the session starts in, as a pair: both true is rotation and
    // position, and cameraunlock::DecodeTrackingMode reads the other two.
    bool rotation_enabled = true;
    bool position_enabled = true;

    // Metres of head travel. Vertical travel is clamped as [-pos_limit_y_down,
    // +pos_limit_y], forward as pos_limit_z and back as pos_limit_z_back.
    float pos_limit_x = cameraunlock::PositionSettings{}.limit_x;
    float pos_limit_y = cameraunlock::PositionSettings{}.limit_y;
    float pos_limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float pos_limit_z = cameraunlock::PositionSettings{}.limit_z;
    float pos_limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    std::string toggle_key{
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault};
    std::string cycle_tracking_mode_key{cameraunlock::config::schema::ConceptTraits<
        cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault};
    std::string yaw_mode_key{
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault};

    // Field of view in degrees for an unzoomed frame, or 0 to render the game's own.
    // Applied as a ratio against the camera's unzoomed angle rather than written flat
    // over the frame's, so iron sights, scopes and scripted cameras keep zooming by the
    // same factor - see fov_override.h.
    float fov_override = 0.0f;

    // Dump the player controller to the log at a fixed interval. A maintenance
    // diagnostic, and the one thing in the mod that writes to the log faster than a
    // player can read it.
    bool state_probe = false;

    // Sample the head pose, the aim resolved in the tracked view and where it projected
    // to, once every two seconds. A session's worth of it is thousands of lines.
    bool aim_geometry_log = false;
};

// [View] Fov: 0, the game's own angle, or 30 to 150 degrees. Below 30 the frame is a
// telescope and tan(fov/2) runs away as 180 approaches, so an angle outside that is not a
// field of view anyone meant.
class FovCodec {
public:
    using Value = float;

    static constexpr float kMin = 30.0f;
    static constexpr float kMax = 150.0f;

    cameraunlock::config::CodecParseResult<float> Parse(std::string_view text) const;
    // Throws std::invalid_argument for a value Parse would not read back.
    std::string Render(float value) const;
    bool Equal(float a, float b) const { return angle_.Equal(a, b); }

private:
    cameraunlock::config::FloatCodec angle_{0.0f, kMax};
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable:
// the mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> ConfigTable();

// What the renderer writes above the settings.
cameraunlock::config::RenderHeader ConfigHeader();

// HeadTracking.ini as the last pre-canonical build read it (src/legacy_config), mapped into
// Config.
cameraunlock::config::LegacyImport<Config> ConfigLegacyImport();

// The owner's options for the files in `folder` (with its trailing separator): the settings in
// CameraUnlock.ini, imported once from HeadTracking.ini, the file every earlier build read and
// that is never written. The mod passes DefaultsFile::PerUser() and a test a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> ConfigOptions(const std::wstring& folder,
                                                               cameraunlock::config::DefaultsFile defaults);

}  // namespace BioShockInfiniteHeadTracking
