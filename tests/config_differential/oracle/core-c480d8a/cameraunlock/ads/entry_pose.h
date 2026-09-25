#pragma once

#include <cmath>

namespace cameraunlock::ads {

// The pose the tracked ADS modes feed the camera: whatever the head is doing,
// measured from the pose the sights came up on.
//
// Both tracked modes make the same swing onto the aim point that Paused makes,
// and then keep tracking from there. That falls out of feeding poses RELATIVE to
// the entry frame: at the moment the sights come up the relative pose is
// identity, which is the same place the paused fade arrives at, and from there
// the head moves the view again. Lowering the weapon hands back the absolute
// pose, so the view swings back by exactly the angle the head is holding.
//
// Four rules, none of them decoration - each was wrong in the first cut of the
// reference mod, and not one of them is visible from a settings or a gate test:
//
//  - **Yaw, pitch and position go relative; roll stays absolute.** Yaw and pitch
//    are the aim axes and zeroing them is the whole point of the snap. Roll
//    moves no aim point, so zeroing it yanks a head tilt the player is actively
//    holding back to level and leans it in again as they move - two horizon
//    jolts per aim, buying nothing.
//  - **Yaw uses the shortest-angle delta.** It arrives wrapped into -180..180,
//    so a plain subtraction reads a 10 degree move across the seam as -350 and
//    whips the view a full turn the wrong way. Pitch is bounded by the tracker's
//    own asin and cannot wrap, so it stays a plain difference.
//  - **Capture from a LIVE rotation.** Interpolators are reset on suppressed
//    frames and return nothing until a fresh packet lands; capturing then
//    freezes a pre-suppression pose and holds the whole aim at that offset. The
//    path that hits it is: aim, open a menu or press the ADS key, move your
//    head, come back with the sights still up.
//  - **Drop the entry pose wherever tracking is suppressed** - menu, cinematic,
//    master toggle, tracker dropout - so the next aim re-enters cleanly instead
//    of resuming against a pose from before the suppression.
//
// Units are the caller's: engine degrees and engine position units, whatever the
// camera boundary hands over. This only ever subtracts.
//
// Pure: no clock, no game, no logging. Driven frame by frame in the tests.
class AdsEntryPose {
public:
    struct Pose {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // Called once per frame with this frame's absolute pose. `live` says the
    // rotation is a real sample rather than the nothing a suppressed frame
    // publishes.
    Pose Relative(bool aiming, bool live, const Pose& absolute) {
        if (!aiming) {
            Reset();
            return absolute;
        }
        if (!m_have) {
            if (!live) return absolute;
            m_entry = absolute;
            m_have = true;
        }
        Pose out;
        out.pitch = absolute.pitch - m_entry.pitch;
        out.yaw = ShortestDeltaDegrees(absolute.yaw, m_entry.yaw);
        out.roll = absolute.roll;
        out.x = absolute.x - m_entry.x;
        out.y = absolute.y - m_entry.y;
        out.z = absolute.z - m_entry.z;
        return out;
    }

    // Drops the entry pose. Called on every frame tracking is suppressed.
    void Reset() { m_have = false; }

    bool HasEntry() const { return m_have; }

    // Signed difference wrapped into -180..180, so a move across the seam is the
    // short way round. fmod rather than a subtract-until loop: a NaN would spin
    // that loop forever on the render thread.
    static float ShortestDeltaDegrees(float a, float b) {
        float d = std::fmod(a - b + 180.0f, 360.0f);
        if (d < 0.0f) d += 360.0f;
        return d - 180.0f;
    }

private:
    bool m_have = false;
    Pose m_entry;
};

}  // namespace cameraunlock::ads
