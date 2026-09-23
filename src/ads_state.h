#pragma once

namespace BioShockInfiniteHeadTracking {

// Is the player aiming down sights, as the game itself understands it.
//
// Polled once per rendered frame from the player controller the scene view is being
// built for - never latched off an enter/exit event. A missed edge would leave the lean
// eased out through hip fire, or in through the aim, and a state machine that
// transitions without firing one is not something the mod can see.
//
// A null controller reports "not aiming": the lean coming back is the safe direction.
bool PlayerIsAiming(const void* playerController);

}  // namespace BioShockInfiniteHeadTracking
