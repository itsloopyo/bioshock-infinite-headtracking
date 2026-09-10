#include "fov_override.h"

#include "build_profile.h"
#include "game_state.h"
#include "logging.h"
#include "memory_probe.h"
#include "minhook_util.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {

namespace {

// APlayerController::GetFOVAngle(), __thiscall with no stack arguments (the function
// ends in a bare `ret`), modelled as __fastcall with a dummy edx so MinHook can detour
// it. A 32-bit MSVC build returns the float in st0 whichever of the two it is declared
// as, so the trampoline's result arrives here as a float and ours leaves the same way.
using GetFovAngle_t = float(__fastcall*)(void* thisptr, void* edx);

GetFovAngle_t g_original = nullptr;
void*         g_target = nullptr;
void*         g_sceneViewCallSite = nullptr;

// The configured angle in degrees, or 0 for "leave the game's own field of view alone".
// Written once at install and read on the game thread.
std::atomic<float> g_requested{0.0f};

// Whether the angle the last rendered frame was built from was the overridden one.
std::atomic<bool> g_activeLastFrame{false};

// Said once. A profile whose angle offset does not fit this build would otherwise show
// up only as "reads as 0.000, which is not an angle" - true about the number, and silent
// about the fact that no read happened at all.
void ReportUnreadableOnce(const char* what, std::size_t offset) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("WARN: the %s field of view at +0x%X is not readable, so the game's own "
              "field of view is rendered and the head pose is not scaled for zoom. The "
              "object layout in this build profile does not fit this EXE.",
              what, static_cast<unsigned>(offset));
}

// Zero when the field could not be read, which every caller's own validation rejects -
// the same page check the zoom flags and the possessed camera go through, and for the
// same reason: the offset is the build profile's, and a dereference past the end of the
// allocation here happens inside the render path.
float ReadFloat(const void* object, std::size_t offset, const char* what) {
    float value = 0.0f;
    if (!ReadProfileField(object, offset, &value)) {
        ReportUnreadableOnce(what, offset);
        return 0.0f;
    }
    return value;
}

// Reported once per unzoomed angle rather than once per session: the angle only changes
// when the player moves the game's own Field of View slider, and a line then says what
// the override became under it. Per frame this is one float comparison.
void ReportApplied(float requested, float baseFov, float gameFov, float scaled) {
    static float s_reportedBase = 0.0f;
    if (s_reportedBase == baseFov) {
        return;
    }
    s_reportedBase = baseFov;
    Log::Line("Field of view override: %.1f over the game's unzoomed %.1f = x%.3f, so "
              "this frame's %.1f renders as %.1f and every zoom is scaled by the same "
              "factor.", requested, baseFov, requested / baseFov, gameFov, scaled);
}

void ReportNoBase(float baseFov) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("WARN: the camera's unzoomed field of view reads as %.3f, which is not an "
              "angle, so [View] Fov cannot be expressed against it and the game's own "
              "field of view is being rendered. Head tracking is unaffected.", baseFov);
}

void ReportNotRenderable(float gameFov, float scaled) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("Field of view left alone on a frame the game rendered at %.1f: the "
              "configured factor takes it to %.1f, which has no projection.",
              gameFov, scaled);
}

float __fastcall Detour(void* thisptr, void* edx) {
    const float gameFov = g_original(thisptr, edx);

    // The accessor has ten call sites and the other nine are game logic - the zoom
    // machinery, and the sensitivity scaling the controller keeps at +0x294. Only the
    // scene-view caller gets a widened angle, so what the player sees changes and what
    // the game does about it does not.
    if (_ReturnAddress() != g_sceneViewCallSite) {
        return gameFov;
    }

    const float requested = g_requested.load(std::memory_order_relaxed);
    const float baseFov = UnzoomedFov(thisptr);
    const FovDecision decision = DecideFov(requested, gameFov, baseFov);
    g_activeLastFrame.store(decision.status == FovOverrideStatus::Applied,
                            std::memory_order_relaxed);
    switch (decision.status) {
        case FovOverrideStatus::Applied:
            ReportApplied(requested, baseFov, gameFov, decision.fov);
            break;
        case FovOverrideStatus::NoBaseAngle:
            ReportNoBase(baseFov);
            break;
        case FovOverrideStatus::NotRenderable:
            ReportNotRenderable(gameFov, decision.fov);
            break;
        case FovOverrideStatus::Off:
            break;
    }
    return decision.fov;
}

}  // namespace

// The angle the frame would be drawn with if nothing were zooming, which is what the
// configured value replaces. Zero when there is no controller, which DecideFov rejects.
//
// GetFOVAngle returns whichever of the zoomed and unzoomed angles is live, so the
// override's ratio has to be taken against the unzoomed one - otherwise aiming down a
// scope would re-scale against the scope's own angle and the widening would compound.
// The zoom compensation reads it for the mirror-image reason: it is the reference the
// live angle is a zoom AGAINST.
//
// Confirmed in the running game: with nothing zooming, the unzoomed field reads the same
// 82.5 the accessor returned.
//
// Both this offset and the controller's own default (which GetFOVAngle returns when no
// camera is possessed) live in the build profile, on the same terms as the RVAs: a patch
// that moves a member has to be expressible as a new profile.
float UnzoomedFov(const void* playerController) {
    if (!playerController) {
        return 0.0f;
    }
    const void* camera = GetPlayerCamera(playerController);
    return camera
        ? ReadFloat(camera, ActiveProfile().offCameraDefaultFov, "camera's unzoomed")
        : ReadFloat(playerController, ActiveProfile().offControllerDefaultFov,
                    "controller's default");
}

bool InstallFovHook(const FovHookTargets& targets, const Config& cfg) {
    if (!(cfg.fov_override > 0.0f)) {
        Log::Line("Field of view: the game's own ([View] Fov is 0), so GetFOVAngle is "
                  "left undetoured.");
        return true;
    }

    g_requested.store(cfg.fov_override, std::memory_order_relaxed);
    g_sceneViewCallSite = reinterpret_cast<void*>(targets.sceneViewCallSite);
    g_target = reinterpret_cast<void*>(targets.getFovAngle);
    const MH_STATUS st = CreateAndEnableHook(g_target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking APlayerController::GetFOVAngle @ 0x%p failed: %d. The "
                  "game's own field of view is rendered; head tracking is unaffected.",
                  g_target, st);
        g_target = nullptr;
        g_original = nullptr;
        return false;
    }
    Log::Line("Field of view hook installed: APlayerController::GetFOVAngle @ 0x%p, "
              "scene-view call site 0x%p, %.1f degrees requested",
              g_target, g_sceneViewCallSite, cfg.fov_override);
    return true;
}

void DisableFovOverride() {
    // A zero request is the off switch DecideFov already tests first, so the detour stays
    // installed and returns the game's own angle from here on - the same shape as
    // DisableCameraTracking, and for the same reason: removing a detour the game is
    // calling cannot be made safe from another thread.
    g_requested.store(0.0f, std::memory_order_relaxed);
    g_activeLastFrame.store(false, std::memory_order_relaxed);
}

bool FovOverrideActive() {
    return g_activeLastFrame.load(std::memory_order_relaxed);
}

}  // namespace BioShockInfiniteHeadTracking
