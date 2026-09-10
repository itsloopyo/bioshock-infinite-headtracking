#pragma once

#include <cstdint>

namespace BioShockInfiniteHeadTracking {

// Detours ULocalPlayer::CalcSceneView and reads the projection matrix out of the
// FSceneView it returns, publishing the two half-field tangents the frame was actually
// projected with.
//
// A field-of-view angle would not be enough on its own: whether the accessor's degrees
// are the horizontal or the vertical angle, and what ratio the vertical half-angle is
// derived with, are conventions the matrix states and the angle does not. Reading
// 1/M[0][0] and 1/M[1][1] settles both, and it keeps working if the game adapts its
// field of view to the display's aspect ratio.
bool InstallSceneViewHook(std::uintptr_t calcSceneViewAddr);

// Byte offset of the projection matrix inside FSceneView, or -1 while it has not been
// found. Logged once so a build whose layout moved can be recognised from the log.
int SceneViewProjectionOffset();

// Times the detour ran. Zero while the camera hook is firing means CalcSceneView was
// reached through a different entry point and the reticle has no projection to use.
unsigned long SceneViewCallCount();

}  // namespace BioShockInfiniteHeadTracking
