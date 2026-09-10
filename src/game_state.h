#pragma once

namespace BioShockInfiniteHeadTracking {

// Why the head pose is or is not being applied this frame.
enum class GameplayState {
    Playing,
    NoCamera,
    Menu,
    // The game is not simulating: a pause screen, a menu over the world, or a load.
    Paused,
};

const char* Describe(GameplayState state);

// The camera the controller possesses, or null when it possesses none.
// Shared rather than dereferenced twice: the field-of-view hook
// reads the camera's unzoomed angle out of the same object this file tests for, and the
// state probe dumps it, and two copies of the offset is one that gets left behind when a
// build moves it.
const void* GetPlayerCamera(const void* playerController);

// Reports whether the player is in gameplay, as opposed to a menu, a pause screen or a
// level transition. playerController is the controller the scene view is being built for
// - the `this` the camera hook's detour receives.
//
// The scene view is built at the full frame rate in the front end and behind the pause
// menu alike, so without this the head pose would keep swinging the camera around while
// the player reads a menu.
//
// The reason is returned rather than a bool because the ways of not being in gameplay
// fail differently, and a report that tracking stopped is only actionable if the log says
// which one.
GameplayState GetGameplayState(const void* playerController);

}  // namespace BioShockInfiniteHeadTracking
