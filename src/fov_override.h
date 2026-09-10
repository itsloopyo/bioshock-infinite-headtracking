#pragma once

#include "config.h"

#include <cmath>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {

// BioShock Infinite's field of view: where the frame's own angle comes from, and how to
// widen it past what the game lets a player ask for.
//
// The game's own Field of View slider is not a number of degrees and its whole travel is
// worth a few percent: with it at maximum the camera measured 82.5 degrees against a 75
// degree reference on the steam-win32-20220511 build. This override is what reaches past
// that.
//
// The angle the frame is drawn with reaches the renderer through
// APlayerController::GetFOVAngle, which ULocalPlayer::CalcSceneView calls once per frame
// while it builds the view. Every other caller of the accessor is game logic, which is
// why the detour is filtered on the scene-view return address and nothing else sees a
// widened angle.
//
// Measured in the running game, at the menu, on that build: the accessor returns 82.5
// and the FSceneView is projected with tanH=1.75395, tanV=0.49330. Scaling the
// accessor's answer by 1.4 for this one caller took the projection to tanH=3.16981,
// tanV=0.89151 - so the angle this override changes is the angle the frame is drawn
// with, and nothing else has to be written for it to land.
//
// What the number means, from the same measurement: the angle is horizontal on a 16:9
// display and the game is Hor+. Both samples project a vertical tangent of exactly
// tan(fov/2)/1.7778 while the horizontal one grows with the display's aspect ratio -
// 3.5556 on the screen it was measured on, twice the 16:9 tangent. So a wider display
// buys horizontal view rather than costing vertical, and the configured number is the
// same number whatever is plugged in.
//
// So this override is a detour on that accessor, gated on the return address of
// CalcSceneView's call, exactly like the viewpoint hook: only the rendered frame gets a
// widened angle, and every raycast, weapon and sensitivity calculation that asks the
// same question keeps the game's own answer.
//
// Nothing has to be kept in sync with it. The reticle is placed from the two half-field
// tangents read out of the FSceneView's own projection matrix (scene_view.h), so a
// widened frame projects the impact point through the matrix it was drawn with, on the
// frame it was drawn on, whatever this override did to the angle.

// What the override did to one frame's angle.
enum class FovOverrideStatus {
    // No override configured. The frame keeps the game's own angle, and no detour is
    // installed at all.
    Off,
    Applied,
    // The unzoomed angle did not read as an angle, so there is nothing to express the
    // override against. The frame keeps the game's own angle.
    NoBaseAngle,
    // Scaling the frame's angle takes it past having a perspective projection - the game
    // is rendering something very wide, not the player's own view. The frame keeps the
    // game's own angle rather than the override being switched off for the session.
    NotRenderable,
};

struct FovDecision {
    FovOverrideStatus status = FovOverrideStatus::Off;
    float fov = 0.0f;
};

// Outside this an angle is not a field of view, so it cannot be the unzoomed one the
// override is expressed against. It is a struct field read out of another process's
// object: a value this far out means the offset no longer fits the build, and scaling by
// it would put a garbage angle in front of the player.
constexpr float kMinBaseFov = 10.0f;
constexpr float kMaxBaseFov = 170.0f;

// At and past 180 degrees tan(fov/2) runs away and there is no projection left.
constexpr float kMaxRenderableFov = 179.0f;

// The angle to render one frame with.
//
// The override is a RATIO against the unzoomed angle, not a value written flat over
// whatever the frame happens to hold. GetFOVAngle does not always return the player's
// own field of view: iron sights, a scoped weapon and a scripted camera each drive it to
// their own value, and writing the configured number in unconditionally would flatten
// every one of them, so aiming down a scope would visibly stop zooming. A ratio shows
// exactly the configured number on a frame at the player's own angle, scales a zoom by
// the same factor as everything else, and is continuous through the transition, so there
// is no frame where the override snaps in or out.
inline FovDecision DecideFov(float requested, float gameFov, float baseFov) {
    if (!(requested > 0.0f)) {
        return { FovOverrideStatus::Off, gameFov };
    }
    if (!std::isfinite(gameFov) || !(gameFov > 0.0f) || !std::isfinite(baseFov)
        || baseFov < kMinBaseFov || baseFov > kMaxBaseFov) {
        return { FovOverrideStatus::NoBaseAngle, gameFov };
    }
    const float scaled = gameFov * (requested / baseFov);
    if (!std::isfinite(scaled) || !(scaled > 0.0f) || scaled >= kMaxRenderableFov) {
        return { FovOverrideStatus::NotRenderable, gameFov };
    }
    return { FovOverrideStatus::Applied, scaled };
}

// ACamera's unzoomed field of view: the angle the camera renders at when no zoom or
// scripted shot is holding it to another one, which is also what GetFOVAngle returns on
// an unzoomed frame. Zero when the controller is null, which every caller's validation
// rejects.
//
// Shared rather than read twice. The override expresses its ratio against this angle and
// the zoom compensation takes its reference from it, and two copies of the offset is one
// that gets left behind when a build moves it - the same reason GetPlayerCamera is
// shared rather than dereferenced in both places.
float UnzoomedFov(const void* playerController);

// The two addresses the field-of-view hook pins, resolved from the matched build
// profile. Grouped for the same reason CameraHookTargets is: they are the same type, and
// swapping them detours the wrong function.
struct FovHookTargets {
    // APlayerController::GetFOVAngle().
    std::uintptr_t getFovAngle;
    // The instruction ULocalPlayer::CalcSceneView returns to after its call to the
    // accessor above. Only that one caller gets a widened angle.
    std::uintptr_t sceneViewCallSite;
};

// Detours APlayerController::GetFOVAngle and widens the angle it returns to the
// scene-view builder. Installs nothing when cfg.fov_override is 0 - the shipped default
// - so a player who has not asked for a different field of view runs a game with one
// fewer function detoured. Returns false only when a configured override could not be
// installed.
bool InstallFovHook(const FovHookTargets& targets, const Config& cfg);

// Whether the last frame's angle was the overridden one. Read by the scene-view hook,
// which reports the projection the frame was really built with once with the game's own
// field of view and once with the override in place - the log line that says the
// override reached the renderer.
// Hands the field of view back to the game. The detour stays installed and starts
// returning the angle the game asked for, because a zero request is already its off
// switch - see DisableCameraTracking for why nothing is unhooked.
void DisableFovOverride();

bool FovOverrideActive();

}  // namespace BioShockInfiniteHeadTracking
