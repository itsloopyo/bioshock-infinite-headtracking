#pragma once

#include "cameraunlock/ads/ads_mode.h"
#include "cameraunlock/ads/entry_pose.h"

namespace cameraunlock::ads {

// What the ADS fade does to the frame's head pose - and the one axis it leaves
// alone.
//
// A pure function, so the walk can be exercised without the game: every answer
// here is a frame the player either sees their head in or does not, and none of
// it is reachable from a test with a game in the loop.
//
// `scale` is AdsFade's: 1 at the hip, 0 with the sights up, easing between the
// two across the transition. `absolute` is this frame's head pose at the engine
// boundary; `relative` is the same pose measured from the frame the sights came
// up on (entry_pose.h). Both are in engine degrees and engine position units.
//
//   paused           the pose fades to nothing and stays there, so the sight
//                    picture is the game's own.
//   marker, tracked  it fades into the entry-relative pose, which is identity at
//                    the moment the sights come up - so the swing onto the aim
//                    is the same one `paused` makes, and head tracking carries
//                    on from there rather than from centre.
//
// **ROLL is in neither fade.** What raising the sights buys is a sight picture
// down the barrel, and a head tilt moves neither the eye off the barrel nor the
// aim point off the middle of the frame: a pure roll leaves the camera's forward
// vector exactly where it was and turns the whole picture about it, gun and
// irons included. Fading it out levels a tilt the player is actively holding and
// leans it back in as the weapon drops - two horizon jolts per aim, buying
// nothing. entry_pose.h already applies that rule in the tracked modes, where
// relative roll IS the absolute roll; `paused` was the odd one out, and in a
// game whose cockpit turns with the head - giving the eye a fixed frame to read
// the horizon against - it read as roll switching off the moment the sights came
// up.
inline AdsEntryPose::Pose BlendAdsPose(AdsMode mode, float scale,
                                       const AdsEntryPose::Pose& absolute,
                                       const AdsEntryPose::Pose& relative) {
    AdsEntryPose::Pose out;
    if (mode == AdsMode::Paused) {
        out.pitch = absolute.pitch * scale;
        out.yaw   = absolute.yaw   * scale;
        // The lean rides the same fade as the rotation rather than being cut at
        // the edge: the sights sit on the muzzle line, so an eye offset from it
        // moves the sight picture off the target, and cutting it in one frame is
        // the jolt the fade exists to remove.
        out.x     = absolute.x * scale;
        out.y     = absolute.y * scale;
        out.z     = absolute.z * scale;
    } else {
        const float rest = 1.0f - scale;
        out.pitch = absolute.pitch * scale + relative.pitch * rest;
        out.yaw   = absolute.yaw   * scale + relative.yaw   * rest;
        out.x     = absolute.x     * scale + relative.x     * rest;
        out.y     = absolute.y     * scale + relative.y     * rest;
        out.z     = absolute.z     * scale + relative.z     * rest;
    }
    out.roll = absolute.roll;
    return out;
}

}  // namespace cameraunlock::ads
