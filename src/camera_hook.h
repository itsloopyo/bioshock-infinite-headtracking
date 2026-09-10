#pragma once

#include "config.h"
#include "tracking_runtime.h"

#include <cstdint>

namespace BioShockInfiniteHeadTracking {

// The two addresses the camera hook pins, resolved from the matched build profile.
// Grouped rather than passed as two bare addresses because they are the same type and
// swapping them detours the wrong function, which crashes the game on the first frame
// with nothing in the log to say why.
struct CameraHookTargets {
    // APlayerController::GetPlayerViewPoint(FVector&, FRotator&).
    std::uintptr_t getPlayerViewPoint;
    // The instruction ULocalPlayer::CalcSceneView returns to after its call to the
    // accessor above. Only that one caller gets the head pose.
    std::uintptr_t sceneViewCallSite;
};

// Detours APlayerController::GetPlayerViewPoint and adds the head pose to the viewpoint
// it returns, but only when the caller is the scene-view builder (identified by
// targets.sceneViewCallSite, the return address of that one call). Everything else -
// aim, traces, audio, interaction - keeps the clean viewpoint, which is what decouples
// where the player looks from where the weapon shoots. Returns false if the detour could
// not be installed.
bool InstallCameraHook(const CameraHookTargets& targets, TrackingRuntime& tracking,
                       const Config& cfg);

// Stands the mod down without unhooking: no pose reaches the camera again and the game's
// own crosshair is handed back on the next rendered frame. The detour stays installed
// because removing one the game is calling is a use-after-free - see the definition.
void DisableCameraTracking();

// True once for each frame the detour handled, and false after it is read. The scene-view
// hook consumes it to tell a frame that carried a head pose from a CalcSceneView that
// never reached the viewpoint call site.
bool ConsumeCameraFrame();

// The [aimgeo] diagnostic line, rate limited to one every couple of seconds. Called from
// the scene-view hook rather than the camera detour, because the screen position it
// reports is derived from the projection that hook publishes.
void ReportAimGeometry();

// Times the detour ran for the scene view, so the heartbeat reports whether the rendered
// view is actually being tracked.
unsigned long CameraHookCallCount();

// Times it ran for any other caller. Non-zero with a zero render count means the scene
// view is taking a different branch and no tracking will be visible.
unsigned long CameraHookNonRenderCallCount();

}  // namespace BioShockInfiniteHeadTracking
