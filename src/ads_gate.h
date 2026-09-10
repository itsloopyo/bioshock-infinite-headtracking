#pragma once

#include "ads.h"
#include "game_state.h"

namespace BioShockInfiniteHeadTracking {

// Whether the head pose reaches the rendered view this frame, and why not when it does
// not.
//
// Pulled out of the camera hook as a pure function so the walk can be exercised without
// the game. What it decides is one branch wide, and every one of its answers is a frame
// the player either sees their head in or does not.
enum class TrackingVerdict {
    // The head pose is applied in full.
    Active,
    // The sights are up in `paused` mode. The pose is still fed to the camera, because
    // it is being EASED off rather than switched off - see AdsFade - and once it has
    // gone the frame is the frame the game would have drawn on its own, bar the head
    // tilt: roll is left out of the fade in every mode, since it moves neither the eye
    // off the barrel nor the aim off the middle of the frame
    // (cameraunlock/ads/ads_blend.h).
    AdsSuspended,
    // The controller possesses no camera.
    NoCamera,
    // A menu or a pause screen over the world.
    GamePaused,
    // Tracking is off, or the tracker has published no pose.
    NoTracker,
};

struct TrackingState {
    TrackingVerdict verdict = TrackingVerdict::NoCamera;
    // The sights are up. Reported in EVERY mode, including `paused` where the gate is
    // closed: the gate says whether tracking applies, this says what the weapon is
    // doing, and the per-frame code needs both - the fade and the marker are driven
    // from it.
    bool aiming = false;
};

// ADS is tested LAST, so a menu or a level transition still reports its own reason when
// both are true at once - and every earlier return leaves `aiming` false, because a
// stale flag through a menu would keep the marker running against a weapon that is not
// raised.
inline TrackingState DecideTracking(GameplayState state, bool havePose, bool aiming,
                                    AdsMode mode) {
    TrackingState s;
    switch (state) {
        case GameplayState::NoCamera: s.verdict = TrackingVerdict::NoCamera;   return s;
        case GameplayState::Menu:
        case GameplayState::Paused:   s.verdict = TrackingVerdict::GamePaused; return s;
        case GameplayState::Playing:  break;
    }
    if (!havePose) {
        s.verdict = TrackingVerdict::NoTracker;
        return s;
    }
    s.aiming = aiming;
    s.verdict = (aiming && AdsSuspendsTracking(mode)) ? TrackingVerdict::AdsSuspended
                                                      : TrackingVerdict::Active;
    return s;
}

// A pose is fed to the camera in both of the first two verdicts. AdsSuspended needs it
// because suspending is an ease-out, not a switch: dropping the pose on the falling edge
// into ADS would throw away the smoothing state, and lowering the weapon would then
// swing the view back through the whole head angle, dozens of times a firefight.
inline bool PoseApplies(TrackingVerdict verdict) {
    return verdict == TrackingVerdict::Active || verdict == TrackingVerdict::AdsSuspended;
}

// Keep correcting the stock reticle until the ADS fade has removed the head pose.
inline bool ShouldDrawAimMarker(bool showAimMarker, bool aiming, AdsMode mode,
                                float poseApplied) {
    if (!showAimMarker) {
        return false;
    }
    if (!aiming) {
        return true;
    }
    if (mode == AdsMode::Marker) {
        return true;
    }
    // Still easing onto the aim: the mark stays until the pose it was placed from has
    // actually gone. Exact rather than approximate - AdsFade returns exactly 0 once it
    // has settled.
    return mode == AdsMode::Paused && poseApplied > 0.0f;
}

}  // namespace BioShockInfiniteHeadTracking
