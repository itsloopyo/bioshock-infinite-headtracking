#pragma once

#include "frame_sample.h"
#include "fov_override.h"

#include "cameraunlock/camera/zoom_compensation.h"

#include <cmath>

namespace BioShockInfiniteHeadTracking {

// Keeps head tracking worth the same amount of screen at any field of view the game
// renders.
//
// BioShock Infinite narrows its field of view to aim. The iron sights do it, the carbine
// and the sniper do it harder, and a scripted shot does it for its own reasons. A narrow
// field magnifies everything in the frame, and head tracking is in the frame: the head
// still turns ten degrees and the camera still turns ten degrees, but the picture moves
// further, by the ratio between the two fields. From the player's seat that is not a
// zoom, it is the mod's sensitivity jumping the moment they aim.
//
// So the pose gets one more conversion at the engine boundary, next to the axis signs
// and the metres-to-centimetres scale: it is shrunk until its displacement ACROSS THE
// SCREEN is what the same pose would have produced at the unzoomed angle. The factor is
// exactly 1 whenever nothing is zooming, so ordinary play is untouched. It is not a
// sensitivity setting, it has no key in the INI, and the tracker's pose is not reshaped -
// what changes is how much engine rotation one tracker degree is worth once the
// projection has had its say.
//
// THE UNITS, which is the part that goes wrong silently. Both angles come from the same
// place: APlayerController::GetFOVAngle for the frame's own, and the camera's unzoomed
// member for the reference, which is the value that accessor returns when nothing is
// zooming. Same accessor family, same units, same axis convention, so whatever that
// convention is drops out of a ratio between them. Measured on steam-win32-20220511: the
// accessor answers 82.5 and the FSceneView it feeds is projected with tanV = 0.49330,
// which is tan(82.5/2) / (16/9), while tanH = 1.75395 = tanV * 3.5556, the display's own
// aspect. So the number is horizontal-at-16:9 and the game is Hor+ - and neither the
// 16/9 reference nor the display's aspect survives the division, which is what makes
// this pairing safe on any screen. The proof is the log line: it reads x1.0000 walking
// around, and a factor pairing two different axes could not.
//
// Yaw, pitch and the positional lean scale. Roll does not: ten degrees of head roll
// rolls the picture ten degrees at every field of view there is, so scaling it would
// only flatten a tilt the player is holding. That split is core's, and so is the
// arithmetic - see cameraunlock/camera/zoom_compensation.h.

// What the field of view did to one frame's pose.
struct ZoomScale {
    // False when neither angle read as an angle. The pose then goes in unscaled and the
    // log says so once: a guessed factor is a silent sensitivity multiplier on every
    // frame of the session, which is worse than no compensation at all.
    bool  known = false;
    float factor = 1.0f;
    // The two angles the renderer is actually using, in degrees, after [View] Fov has
    // had its say. Carried so the log line can show every term of the ratio.
    float renderedFov = 0.0f;
    float renderedBaseFov = 0.0f;
};

// The factor for one frame, from the angle the accessor returned and the camera's
// unzoomed one, both raw and in the game's own units.
//
// [View] Fov is applied to BOTH before the ratio is taken, because the override is a
// multiplier on the ANGLE and tan is not linear: at a 1.4x override an 82.5 degree frame
// renders at 115.5 and a 45 degree zoom renders at 63, and the ratio between those two
// is not the ratio between 82.5 and 45. Taking it before the override would leave the
// compensation wrong by exactly the amount the player widened their view, which is the
// one case a player who changed a setting would blame on the setting.
// The widest angle the tangent round trip may be handed. ScaleAngleForZoom is
// atan(tan(a) * factor), which is only the identity on the open interval either side of
// vertical: at 95 degrees tan has already crossed its asymptote, so the round trip returns
// -85 and the view snaps to the opposite side. Core's own contract says the input must be
// within +/-90 and nothing on this side was enforcing it. Reachable in ordinary play -
// a tracker profile mapped out past 90 for a look-behind, and the relative pose that
// `marker` and `tracked` measure from the entry frame, which is bounded to +/-180.
constexpr float kMaxScalableAngleDeg = 89.0f;

// The narrowest live angle still taken as a field of view. A scope legitimately renders
// far below the base's build-fit floor - a 7.5x on a 75 degree base is 10 degrees - so
// the only thing this has to exclude is an angle so small it cannot be a frame at all.
constexpr float kMinRenderedFov = 1.0f;

inline ZoomScale DecideZoomScale(float gameFov, float gameBaseFov, float requestedOverride) {
    ZoomScale out;

    // The REFERENCE is bounded by kMinBaseFov, which is a build-fit test: a camera whose
    // unzoomed angle reads below it is a struct offset that no longer fits the build, not
    // a camera. The LIVE angle is bounded far lower, because a narrow live angle is the
    // whole point - it is what a scope IS. Sharing the base's bound here switched the
    // compensation off at exactly the magnifications it exists for, and left the pose
    // going in unscaled at the moment it needed scaling most: well behaved at iron
    // sights, violently over-sensitive through a scope.
    const auto isBaseAngle = [](float deg) {
        return std::isfinite(deg) && deg >= kMinBaseFov && deg <= kMaxBaseFov;
    };
    const auto isLiveAngle = [](float deg) {
        return std::isfinite(deg) && deg >= kMinRenderedFov && deg <= kMaxBaseFov;
    };
    if (!isLiveAngle(gameFov) || !isBaseAngle(gameBaseFov)) {
        return out;
    }

    // .fov is the angle the frame is drawn with whatever the override decided, including
    // the cases where it declined, so both of these are the renderer's own numbers.
    const FovDecision now = DecideFov(requestedOverride, gameFov, gameBaseFov);
    const FovDecision base = DecideFov(requestedOverride, gameBaseFov, gameBaseFov);

    // Both angles have to have come from the same decision, or the ratio prices a frame
    // against a reference the renderer never used. The two calls gate independently: a
    // wide shot can scale past kMaxRenderableFov and fall back to the game's own angle
    // while the reference is still the overridden one, and the ratio between those two is
    // not a zoom at all. Worked through at a 2x override on a 75 degree base, a 90 degree
    // frame yields 0.268 where the frame actually drawn wants 1.303 - tracking five times
    // too weak, with the log's own gate line already latched from an earlier frame.
    if (now.status != base.status) {
        return out;
    }
    out.renderedFov = now.fov;
    out.renderedBaseFov = base.fov;
    if (!isLiveAngle(out.renderedFov) || !isBaseAngle(out.renderedBaseFov)) {
        return out;
    }

    constexpr float kHalfDegToRad = 3.14159265358979323846f / 360.0f;
    const float tanNow = std::tan(out.renderedFov * kHalfDegToRad);
    const float tanBase = std::tan(out.renderedBaseFov * kHalfDegToRad);
    if (!(tanNow > 0.0f) || !(tanBase > 0.0f)) {
        return out;
    }

    const float factor = cameraunlock::camera::FovZoomFactor(tanNow, tanBase);
    if (!std::isfinite(factor) || !(factor > 0.0f)) {
        return out;
    }
    out.known = true;
    out.factor = factor;
    return out;
}

// One angle through the round trip, with the INPUT clamped into the round trip's domain
// rather than the decision to use it.
//
// Returning a wide angle unscaled instead would put a step in the middle of the pose: at a
// factor of 0.1, 88.999 degrees scales to 80.089 while 89.001 passes through as 89.001, so
// two head positions a five-hundredth of a degree apart write angles nine degrees apart.
// Clamping the input keeps the function continuous and monotone, which is what a pose
// being written into a camera every frame has to be.
inline float ScaleAngleForZoomBounded(float deg, float factor) {
    if (!std::isfinite(deg)) {
        return deg;
    }
    const float bounded = deg > kMaxScalableAngleDeg    ? kMaxScalableAngleDeg
                          : deg < -kMaxScalableAngleDeg ? -kMaxScalableAngleDeg
                                                        : deg;
    return cameraunlock::camera::ScaleAngleForZoom(bounded, factor);
}

// The pose as it will be written into the engine. Yaw and pitch through the tangent
// round trip, position by a straight multiply, roll passed through untouched.
inline FrameSample ScaleForZoom(const FrameSample& s, float factor) {
    // Nothing is zooming, so nothing is scaled. Not just an optimisation: the round trip
    // is lossy outside its domain, and running it at a factor of exactly 1 would corrupt
    // a wide pose on every frame of ordinary play, where the log correctly reads x1.0000.
    if (factor == 1.0f) {
        return s;
    }
    FrameSample out = s;
    out.yaw = ScaleAngleForZoomBounded(s.yaw, factor);
    out.pitch = ScaleAngleForZoomBounded(s.pitch, factor);
    out.pos_x = s.pos_x * factor;
    out.pos_y = s.pos_y * factor;
    out.pos_z = s.pos_z * factor;
    return out;
}

// The factor for the frame being built for this player controller: reads the angle the
// scene-view builder is about to use, reads the camera's unzoomed one, and reports both
// terms to the log once unzoomed and once zoomed.
//
// Called from the camera hook, on the game thread, once per rendered frame.
ZoomScale ZoomScaleForFrame(const void* playerController, float requestedOverride);

}  // namespace BioShockInfiniteHeadTracking
