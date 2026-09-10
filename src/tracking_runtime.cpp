#include "tracking_runtime.h"

#include "logging.h"

#include <chrono>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {

void TrackingRuntime::Start(const Config& cfg) {
    m_cfg = cfg;

    // No sensitivity, deadzone or inversion is set: the core defaults are 1:1 with no
    // deadzone and no inversion, which is the pose the tracker sent. Shaping it is the
    // tracker app's job, so one profile behaves the same in every game.
    cameraunlock::PositionSettings pos;
    pos.limit_x = m_cfg.pos_limit_x;
    pos.limit_y = m_cfg.pos_limit_y;
    pos.limit_y_down = m_cfg.pos_limit_y_down;
    pos.limit_z = m_cfg.pos_limit_z;
    pos.limit_z_back = m_cfg.pos_limit_z_back;
    m_session.SetPositionSettings(pos);

    // One value feeds both the rotation and the position processor - there is no
    // separate position smoothing setting - picked per connection from the
    // receiver's IsRemoteConnection(), re-read on every Update().
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or smoothing silently stays local");
    m_session.SetLocalSmoothing(m_cfg.local_smoothing);
    m_session.SetRemoteSmoothing(m_cfg.remote_smoothing);

    m_worldSpaceYaw.store(m_cfg.world_space_yaw, std::memory_order_relaxed);
    m_adsMode.store(m_cfg.ads_mode, std::memory_order_relaxed);
    m_session.SetMode(m_cfg.position_enabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);

    m_receiver.SetLog([](const std::string& msg) {
        Log::Line("UDP: %s", msg.c_str());
    });

    m_started.store(true, std::memory_order_release);

    // LAST, and with a release. The camera detour is already installed by the time this
    // runs, so the render thread is free to enter SampleFrame the instant this reads
    // true - and the first thing it does there is read m_cfg, which is a plain struct
    // written at the top of this function. A relaxed store publishes no ordering, so
    // that read would be a data race on non-atomic memory; the release pairs with the
    // acquire in SampleFrame and makes every write above visible before it.
    m_enabled.store(m_cfg.enabled_on_startup, std::memory_order_release);

    if (m_receiver.Start(m_cfg.udp_port)) {
        Log::Line("UDP receiver listening on port %u", m_cfg.udp_port);
    } else {
        Log::Line("WARN: UDP receiver did not bind immediately on port %u; background retry active", m_cfg.udp_port);
    }
}

void TrackingRuntime::Stop() {
    // Claiming the flag leaves exactly one caller to do the teardown: the receiver's own
    // Stop() tests a flag before joining, so two callers arriving together would join one
    // std::thread twice.
    if (m_started.exchange(false, std::memory_order_acq_rel)) {
        m_receiver.Stop();
    }
    // Cleared too, or a method called Stop leaves the thing it stopped switched on: the
    // render thread can still reach SampleFrame between here and the camera standing
    // down, and would tick the frame clock against a receiver that has gone.
    m_enabled.store(false, std::memory_order_release);
}

void TrackingRuntime::ToggleEnabled() {
    const bool prev = m_enabled.load(std::memory_order_relaxed);
    m_enabled.store(!prev, std::memory_order_relaxed);
    Log::Line("Tracking %s", !prev ? "enabled" : "disabled");
}

void TrackingRuntime::CycleTrackingMode() {
    switch (m_session.CycleMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition:
            Log::Line("Tracking mode: rotation + position (6DOF)");
            break;
        case cameraunlock::TrackingMode::RotationOnly:
            Log::Line("Tracking mode: rotation only");
            break;
        case cameraunlock::TrackingMode::PositionOnly:
            Log::Line("Tracking mode: position only");
            break;
    }
}

// The mode is read fresh by the camera hook every frame, so nothing has to be
// invalidated here for a press mid-aim to take effect on that aim.
void TrackingRuntime::CycleAdsMode() {
    const AdsMode next = NextAdsMode(m_adsMode.load(std::memory_order_relaxed));
    m_adsMode.store(next, std::memory_order_relaxed);
    Config::SaveAdsMode(next);
    Log::Line("ADS mode: %s", AdsModeValue(next));
}

void TrackingRuntime::ToggleYawMode() {
    const bool prev = m_worldSpaceYaw.load(std::memory_order_relaxed);
    m_worldSpaceYaw.store(!prev, std::memory_order_relaxed);
    Log::Line("Yaw mode: %s", !prev ? "world-space (horizon-locked)" : "camera-local");
}

bool TrackingRuntime::IsPoseFresh() const {
    const std::int64_t lastUs = m_receiver.GetLastReceiveTimestamp();
    if (lastUs == 0) {
        return false;
    }
    const std::int64_t nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (nowUs - lastUs) / 1000 < m_cfg.data_freshness_ms;
}

FrameSample TrackingRuntime::SampleFrame() {
    // Switched off is not tracking lost: the player asked for the game's own camera
    // back, so the held pose is dropped rather than re-applied on the next enable.
    // Acquire, paired with the release at the end of Start(): everything Start() wrote
    // into m_cfg and the session must be visible before the first frame reads it.
    if (!m_enabled.load(std::memory_order_acquire)) {
        m_held = FrameSample{};
        return m_held;
    }

    // Ticked unconditionally, including on a held frame. Skipping it while the tracker
    // is quiet makes the first fresh frame arrive with the whole gap as its delta,
    // clamped to kMaxFrameDtSec - and at 0.25s the smoothing factor is 0.99998, so the
    // view snaps to the new pose instead of blending back to it. That is the snap the
    // hold exists to prevent, moved to the other end of the gap.
    const float dt = m_clock.Tick();

    // A tracker that has gone quiet holds its last pose instead of snapping the view
    // back to the game's camera. The face leaving the webcam for half a second, or two
    // dropped datagrams over WiFi, otherwise throws the view across the screen and back
    // again - and with smoothing running only while samples arrive, nothing blends it.
    if (!IsPoseFresh() || !m_session.Update(dt)) {
        return HeldForCurrentMode();
    }

    FrameSample out;
    out.has_rotation = m_session.GetRotation(out.yaw, out.pitch, out.roll);
    out.has_position = m_session.GetPositionOffset(out.pos_x, out.pos_y, out.pos_z);
    m_held = out;
    return out;
}

// The held pose predates whatever mode the player has since cycled to, and a held frame
// never reaches m_session.Update(), which is where the mode is normally applied. Without
// this, cycling to rotation-only while the tracker is quiet leaves the stale lean on
// screen until packets resume - the player presses the key precisely because the lean is
// in the way, and nothing happens.
FrameSample TrackingRuntime::HeldForCurrentMode() const {
    FrameSample out = m_held;
    if (!m_session.IsRotationActive()) {
        out.has_rotation = false;
        out.yaw = out.pitch = out.roll = 0.0f;
    }
    if (!m_session.IsPositionActive()) {
        out.has_position = false;
        out.pos_x = out.pos_y = out.pos_z = 0.0f;
    }
    return out;
}

}  // namespace BioShockInfiniteHeadTracking
