#pragma once

#include "frame_sample.h"

#include "cameraunlock/ads/ads_fade.h"

namespace BioShockInfiniteHeadTracking {

using cameraunlock::ads::AdsFade;

// The lean eased out while the sights are up, rotation untouched.
//
// The weapon is posed from the clean eye, so a lean moves the rendered eye off its sight
// line. The mod has no weapon pass of its own to redraw it from, so the lean goes instead:
// x, y and z are scaled by the fade, which is 1 at the hip and 0 with the sights up.
// Rotation, roll included, passes through at full strength in every state - a rotation
// about the eye keeps the sights lined up, just off-centre where the weapon points.
inline FrameSample EaseLeanForAds(const FrameSample& s, float leanScale) {
    FrameSample out = s;
    out.pos_x *= leanScale;
    out.pos_y *= leanScale;
    out.pos_z *= leanScale;
    return out;
}

}  // namespace BioShockInfiniteHeadTracking
