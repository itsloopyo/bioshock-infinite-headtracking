#pragma once

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
    // The controller possesses no camera.
    NoCamera,
    // A menu or a pause screen over the world.
    GamePaused,
    // Tracking is off, or the tracker has published no pose.
    NoTracker,
};

struct TrackingState {
    TrackingVerdict verdict = TrackingVerdict::NoCamera;
    // The sights are up. Never closes the gate: it only decides how far the lean is
    // eased out (see EaseLeanForAds).
    bool aiming = false;
};

// ADS is tested LAST, so a menu or a level transition still reports its own reason when
// both are true at once - and every earlier return leaves `aiming` false, because a
// stale flag through a menu would hold the lean eased out against a weapon that is not
// raised.
inline TrackingState DecideTracking(GameplayState state, bool havePose, bool aiming) {
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
    s.verdict = TrackingVerdict::Active;
    return s;
}

}  // namespace BioShockInfiniteHeadTracking
