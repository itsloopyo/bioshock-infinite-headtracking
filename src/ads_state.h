#pragma once

namespace BioShockInfiniteHeadTracking {

// Is the player aiming down sights, as the game itself understands it.
//
// Polled once per rendered frame from the player controller the scene view is being
// built for - never latched off an enter/exit event. A missed edge either strands the
// player in ADS behaviour or leaks hip-fire tracking into the aim, and a state machine
// that transitions without firing one is not something the mod can see.
//
// A null controller reports "not aiming", which is the direction that fails toward the
// game's own camera.
bool PlayerIsAiming(const void* playerController);

}  // namespace BioShockInfiniteHeadTracking
