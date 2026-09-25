#pragma once

#include <cstdint>

#include "ads.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace BioShockInfiniteHeadTracking {

// The shipped defaults, in one place. WriteDefaultIni writes these, LoadOrCreate
// falls back to them, and Config's members are initialised from them, so a
// default-constructed Config, a freshly written INI and a read of a key that is
// missing from the file cannot disagree about what the default is.
namespace defaults {
constexpr bool  kEnableOnStartup = true;
constexpr int   kPort            = 4242;
constexpr int   kMinPort         = 1024;
constexpr int   kMaxPort         = 65535;
constexpr int   kDataFreshnessMs = 500;
constexpr bool  kWorldSpaceYaw   = true;
constexpr bool  kShowAimMarker   = true;
constexpr float kLocalSmoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kRemoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);
constexpr int   kVkToggle        = 0x23; // VK_END
constexpr int   kVkCycleMode     = 0x21; // VK_PRIOR (Page Up)
constexpr int   kVkYawMode       = 0x22; // VK_NEXT (Page Down)
constexpr int   kVkAdsMode       = 0x2D; // VK_INSERT
constexpr bool  kChord           = true;
// What head tracking does while the sights are up. The default is the mode that cannot
// be wrong: the game keeps the camera for the whole aim, so the sight picture is exactly
// the game's. The cycle, the value strings and the toast wording are core's - see
// ads.h.
constexpr AdsMode kAdsMode       = kDefaultAdsMode;

// Field of view in degrees, or 0 for "leave the game's own field of view alone", which
// is what ships. The game's only FOV control is a 0-to-1 slider its own config file caps
// at 15 percent of the angle already being rendered (see fov_override.h), so anything
// wider than that has to come from here.
constexpr float kFovOverride    = 0.0f;
// A configured angle outside this is a typo rather than a preference: below 30 the frame
// is a telescope, and tan(fov/2) runs away as 180 approaches. Same range, same key name
// and same 0-is-off as the other shooters in the fleet, so a player who has set it in
// one finds it here.
constexpr float kMinFovOverride = 30.0f;
constexpr float kMaxFovOverride = 150.0f;

// The state probe writes several kilobytes a second, so it is off in every shipped
// INI. It is the tool for finding the field that separates gameplay from a menu on a
// build whose layout has moved - turned on for one session, then off again.
constexpr bool  kStateProbe      = false;

// The aim-geometry sample is a line every two seconds for as long as the mod is
// injecting, so it is off in every shipped INI as well. It is what settles a "the mark
// is in the wrong place" report, and it is only wanted while someone is settling one.
constexpr bool  kAimGeometryLog  = false;

constexpr bool  kPositionEnabled = true;
constexpr float kPosLimitX       = cameraunlock::PositionSettings{}.limit_x;
constexpr float kPosLimitY       = cameraunlock::PositionSettings{}.limit_y;
constexpr float kPosLimitYDown   = cameraunlock::PositionSettings{}.limit_y_down;
constexpr float kPosLimitZ       = cameraunlock::PositionSettings{}.limit_z;
constexpr float kPosLimitZBack   = cameraunlock::PositionSettings{}.limit_z_back;
}  // namespace defaults

struct Config {
    bool  enabled_on_startup = defaults::kEnableOnStartup;
    uint16_t udp_port = static_cast<uint16_t>(defaults::kPort);

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine (loopback) uses local_smoothing, a remote network
    // device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = defaults::kLocalSmoothing;
    float remote_smoothing = defaults::kRemoteSmoothing;

    // Move the stock crosshair with the projected aim direction.
    bool show_aim_marker = defaults::kShowAimMarker;
    int  data_freshness_ms = defaults::kDataFreshnessMs;

    // true = horizon-locked (world-space) yaw, false = camera-local yaw.
    bool world_space_yaw = defaults::kWorldSpaceYaw;

    // What head tracking does while the sights are up: paused -> marker -> tracked.
    // Cycled in game, and written back to the INI on every press, so the player's choice
    // survives a restart.
    AdsMode ads_mode = defaults::kAdsMode;

    // Field of view in degrees for an unzoomed frame, or 0 to render the game's own.
    // Applied as a ratio against the camera's unzoomed angle rather than written flat
    // over the frame's, so iron sights, scopes and scripted cameras keep zooming by the
    // same factor - see fov_override.h.
    float fov_override = defaults::kFovOverride;

    // Dump the player controller to the log at a fixed interval. Off in every shipped
    // INI: it is a maintenance diagnostic, and the one thing in the mod that writes to
    // the log faster than a player can read it.
    bool  state_probe = defaults::kStateProbe;

    // Sample the head pose, the aim resolved in the tracked view and where it projected
    // to, once every two seconds. Off in every shipped INI: a session's worth of it is
    // thousands of lines, and the events a player would report sit between them.
    bool  aim_geometry_log = defaults::kAimGeometryLog;

    // 6DOF positional tracking.
    bool  position_enabled = defaults::kPositionEnabled;
    float pos_limit_x = defaults::kPosLimitX;
    // Vertical travel is clamped as [-pos_limit_y_down, +pos_limit_y]. The two are
    // separate keys because a player sitting down has less room to duck than to
    // stretch up, and mirroring one into the other would hide that.
    float pos_limit_y = defaults::kPosLimitY;
    float pos_limit_y_down = defaults::kPosLimitYDown;
    float pos_limit_z = defaults::kPosLimitZ;
    float pos_limit_z_back = defaults::kPosLimitZBack;

    int vk_toggle     = defaults::kVkToggle;
    int vk_cycle_mode = defaults::kVkCycleMode;
    int vk_yaw_mode   = defaults::kVkYawMode;
    int vk_ads_mode   = defaults::kVkAdsMode;
    bool chord_toggle = defaults::kChord;
    bool chord_cycle_mode = defaults::kChord;
    bool chord_yaw_mode = defaults::kChord;
    bool chord_ads_mode = defaults::kChord;

    bool LoadOrCreate(const char* iniPath);

    // Writes just the ADS mode back, leaving every other key and every comment in the
    // file alone. Called from the hotkey handler, so it goes through the INI path the
    // last LoadOrCreate used rather than deriving one of its own.
    static void SaveAdsMode(AdsMode mode);
};

}  // namespace BioShockInfiniteHeadTracking
