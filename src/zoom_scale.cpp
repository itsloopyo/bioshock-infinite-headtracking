#include "zoom_scale.h"

#include "aim_marker.h"
#include "build_profile.h"
#include "game_module.h"
#include "logging.h"

#include <cmath>

namespace BioShockInfiniteHeadTracking {

namespace {

// APlayerController::GetFOVAngle(), __thiscall with no stack arguments, modelled as
// __fastcall with a dummy edx - the same shape fov_override.cpp detours it in.
using GetFovAngle_t = float(__fastcall*)(const void* thisptr, void* edx);

// The accessor the scene-view builder itself calls a few instructions later, so this is
// the angle the frame about to be drawn will be projected with rather than a value
// sampled from somewhere adjacent.
//
// Called through its own address rather than through the field-of-view hook's
// trampoline, and that is deliberate on both counts. With [View] Fov off no trampoline
// exists at all, so there would be nothing to call; with it on, the call lands in that
// detour, which compares its return address against the scene-view call site, does not
// match one in this module, and hands back the game's own unwidened angle - which is
// exactly the number wanted here, because DecideZoomScale applies the override to it
// itself. One resolution path, right in both configurations.
GetFovAngle_t FovAngle() {
    static const GetFovAngle_t fn =
        GameFunction<GetFovAngle_t>(ActiveProfile().rvaGetFovAngle);
    return fn;
}

// Every term of the ratio, on one line a reader can check with a calculator.
//
// Latched twice rather than once, because one line cannot prove this: a factor that is
// wrong by a constant reads exactly like a factor that is right, and the whole claim is
// that the number is 1.0000 when the game is not zooming and something else when it is.
// So the log carries one line from an unzoomed frame - the gate, which must read
// x1.0000 - and one from the first frame the game actually zooms, which is the evidence
// that the compensation is doing anything at all.
//
// The tangents at the end are the previous frame's, read back off the FSceneView the
// renderer built. They are the cross-check on the units claim: tanV must be
// tan(rendered/2) divided by 16/9, and tanH must be that times the display's aspect. If
// those do not hold, the two angles in this line are not the pair the ratio should be
// taken between.
void ReportOnce(const ZoomScale& z, float gameFov, float gameBaseFov, float requested) {
    static bool s_reportedUnzoomed = false;
    static bool s_reportedZoomed = false;
    const bool zoomed = std::fabs(z.factor - 1.0f) > 0.01f;
    bool& latch = zoomed ? s_reportedZoomed : s_reportedUnzoomed;
    if (latch) {
        return;
    }

    // Held back, unlatched, until the renderer has published a projection. This runs
    // from the viewpoint accessor, which the scene-view builder calls BEFORE it finishes
    // the FSceneView, so on the very first gameplay frame the tangents are still zero -
    // and a cross-check printed as 0.00000 is worse than one printed a frame later,
    // because the line looks complete and settles nothing.
    const AimMarker& marker = GetAimMarker();
    if (!marker.projection_valid.load(std::memory_order_acquire)) {
        return;
    }
    latch = true;

    Log::Line("[zoom] %s frame: the accessor says %.2f deg against the camera's unzoomed "
              "%.2f deg; [View] Fov %s, so the renderer draws %.2f against an unzoomed "
              "%.2f, and the head pose is scaled x%.4f (yaw, pitch and lean; roll is "
              "left alone). Both angles are the same accessor's, so the 16:9 reference "
              "and the display's aspect cancel. Last projected frame: tanH=%.5f "
              "tanV=%.5f.",
              zoomed ? "zoomed" : "unzoomed", gameFov, gameBaseFov,
              requested > 0.0f ? "on" : "off", z.renderedFov, z.renderedBaseFov, z.factor,
              marker.tan_half_h.load(std::memory_order_relaxed),
              marker.tan_half_v.load(std::memory_order_relaxed));
}

// Said once, and it matters: with no factor the pose goes in unscaled, so aiming down
// the sights magnifies head tracking by however much the game zooms and the player has
// no way to tell that from the mod simply behaving that way.
void ReportUnknownOnce(float gameFov, float gameBaseFov) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("WARN: the field of view reads as %.3f against an unzoomed %.3f, and "
              "neither pair of numbers is an angle a frame is drawn with, so the head "
              "pose is not being scaled for zoom. Head tracking works; it will feel "
              "stronger while the sights are up.", gameFov, gameBaseFov);
}

}  // namespace

ZoomScale ZoomScaleForFrame(const void* playerController, float requestedOverride) {
    const GetFovAngle_t accessor = FovAngle();
    if (!accessor || !playerController) {
        return ZoomScale{};
    }
    const float gameFov = accessor(playerController, nullptr);
    const float gameBaseFov = UnzoomedFov(playerController);

    const ZoomScale z = DecideZoomScale(gameFov, gameBaseFov, requestedOverride);
    if (!z.known) {
        ReportUnknownOnce(gameFov, gameBaseFov);
        return z;
    }
    ReportOnce(z, gameFov, gameBaseFov, requestedOverride);
    return z;
}

}  // namespace BioShockInfiniteHeadTracking
