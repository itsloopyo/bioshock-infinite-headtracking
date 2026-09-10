#include "camera_hook.h"

#include "ads.h"
#include "ads_gate.h"
#include "ads_state.h"
#include "aim_marker.h"
#include "aim_projection.h"
#include "game_crosshair.h"
#include "game_state.h"
#include "head_pose.h"
#include "logging.h"
#include "minhook_util.h"
#include "ue3_rotation.h"
#include "ue3_types.h"
#include "zoom_scale.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cmath>

namespace BioShockInfiniteHeadTracking {

namespace {

// APlayerController::GetPlayerViewPoint(FVector& out_Location, FRotator& out_Rotation),
// __thiscall with both out-pointers on the stack (the function ends `ret 8`), modelled
// as __fastcall with a dummy edx so MinHook can detour it.
//
// The engine writes 16 bytes through each pointer - the twelve that make up the vector
// or the rotator, and a fourth dword copied from the controller. Only the first twelve
// are touched here.
using GetPlayerViewPoint_t = void(__fastcall*)(void* thisptr, void* edx,
                                               UE3Vector* outLoc, UE3Rotator* outRot);

GetPlayerViewPoint_t g_original = nullptr;
void*            g_target = nullptr;
void*            g_sceneViewCallSite = nullptr;
// Read on the game thread inside the detour and cleared on unload. Atomic, and loaded
// once into a local for the whole detour: a plain pointer tested and then dereferenced
// lets the clear land between the two.
std::atomic<TrackingRuntime*> g_tracking{nullptr};
// Set when the mod gives up after the hooks are already live. The detour keeps running -
// see DisableCameraTracking for why it is not removed - and this is what makes it do
// nothing but hand the game's own crosshair back.
std::atomic<bool> g_inert{false};
std::atomic<bool> g_frameFresh{false};
bool             g_showAimMarker = true;
// [Diagnostics] AimGeometry. Off unless a player has been asked to turn it on, because
// unlike everything else the mod logs it is a running sample rather than an event: it
// says nothing new between one line and the next, and a session of it buries the lines
// that do.
bool             g_aimGeometryLog = false;
// The configured [View] Fov, taken at install alongside the marker flag rather than read
// out of the field-of-view module. That module is installed AFTER this one (see
// InitThread), so asking it would leave the first frames of a session computing the zoom
// factor against an override the player had configured and this hook had not yet seen.
float            g_fovOverride = 0.0f;
// Incremented in the detour on the game thread and read on the heartbeat thread. Plain
// longs here are a data race whose practical cost is a wrong diagnosis rather than a
// wrong number: nothing stops the heartbeat's read being hoisted out of its loop, which
// latches "Camera detour: not firing" for a session in which it fires every frame.
std::atomic<unsigned long> g_totalCalls{0};
std::atomic<unsigned long> g_renderCalls{0};

void ShowGameCrosshairOnly() {
    GetAimMarker().active.store(false, std::memory_order_relaxed);
    RestoreGameCrosshair();
}

// The ADS transition and the pose the sights came up on. Touched only from the detour,
// which the game calls on its own thread while it builds the scene view, so neither
// needs to be atomic.
AdsFade g_adsFade;
AdsEntryPose g_adsEntry;

// Milliseconds from the performance counter rather than GetTickCount64. The ADS ease
// runs for about 150 ms and the tick count moves in steps of roughly 15.6, so it would
// resolve the whole swing onto ten values - visible as stepping at any frame rate above
// 60. The counter is read once per frame and costs nothing next to the frame it is in.
unsigned long long NowMs() {
    static const long long frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<unsigned long long>(now.QuadPart * 1000 / frequency);
}

// The clean aim in the tracked view's basis, as the frame computed it.
struct AimComponents {
    float forward = 1.0f;
    float right = 0.0f;
    float up = 0.0f;
};

// Where the shot goes in the head-tracked view.
//
// The reticle marks a DIRECTION, not a measured point, and that is a decision rather than
// an omission - aim_marker.h states what it costs and the two conditions for changing it.
// The clean camera's forward axis is the direction the round leaves along; resolving it
// into the tracked view's own basis is the whole placement.
//
// The two bases come from the same RotatorToMatrix the injection used, so the mark cannot
// drift out of agreement with the camera on a combined pose the way a per-axis Euler
// formula does.
//
// `drawMarker` is derived per frame by the caller and never latched: with the sights up
// the mark belongs on screen in `marker` mode alone, and in the other two the game's own
// crosshair is the only thing claiming to mark the shot.
//
// The components are handed back as well as stored, so the diagnostic below reports the
// numbers this frame resolved rather than loading them straight back out of the atomics
// they were just written to.
AimComponents PublishAimMarker(const UE3Rotator& clean, const UE3Rotator& tracked,
                               bool drawMarker) {
    const Mat3 trackedBasis = RotatorToMatrix(tracked);

    // Only the clean rotator's forward axis is wanted, so the other two rows are never
    // built. It is already a unit vector, which is what ProjectAim's kMinForward guard
    // needs to be an angle rather than a length.
    float aim[3];
    ForwardAxis(clean, aim);

    AimComponents resolved;
    ResolveInBasis(trackedBasis, aim, &resolved.forward, &resolved.right, &resolved.up);

    AimMarker& marker = GetAimMarker();
    marker.forward.store(resolved.forward, std::memory_order_relaxed);
    marker.right.store(resolved.right, std::memory_order_relaxed);
    marker.up.store(resolved.up, std::memory_order_relaxed);
    // Release, paired with the acquire in ProjectAim. Relaxed stores to distinct atomics
    // may be reordered by the compiler, so `active` could otherwise be observed true
    // alongside the previous frame's - or, on the first activation, the initial -
    // components.
    marker.active.store(drawMarker, std::memory_order_release);
    return resolved;
}

// One line carrying every term the placement is made of, so a "the mark is off" report
// is settled by arithmetic rather than by argument: the head pose that moved the view,
// the aim resolved in that view, where it projected to, and the lean the placement is
// NOT correcting for. Rate-limited rather than edge-triggered, because what is wanted is
// a sample of a pose the player was holding while they looked at the mark.
constexpr unsigned long long kAimGeometryIntervalMs = 2000;

// This frame's terms, held until the projection it was drawn with exists. Game thread
// only, like everything else the detour touches.
struct AimGeometrySample {
    FrameSample pose;
    AimComponents aim;
    Lean lean;
};
AimGeometrySample g_aimGeometry;

constexpr unsigned long kFramesBeforeNoProjectionReport = 300;

void ReportNoProjectionOnce(bool wanted) {
    static unsigned long frames = 0;
    static bool reported = false;
    if (reported || !wanted || ++frames < kFramesBeforeNoProjectionReport) {
        return;
    }
    reported = true;
    Log::Line("WARN: stock crosshair correction has been wanted for %lu frames and no projection has "
              "been published to place it with, so the game's own crosshair is being left "
              "at its native position. The scene-view hook is what publishes one. Head tracking itself "
              "is unaffected.", frames);
}

void StashAimGeometry(const FrameSample& s, const AimComponents& aim, const Lean& lean) {
    if (!g_aimGeometryLog) {
        return;
    }
    g_aimGeometry.pose = s;
    g_aimGeometry.aim = aim;
    g_aimGeometry.lean = lean;
}

// The pose is dropped rather than written into the engine, and the reason is named once:
// a view that simply stops moving is otherwise indistinguishable from a dead tracker.
void ReportNonFiniteOnce(const FrameSample& s, float zoomFactor) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("WARN: the pose is not a finite number "
              "(rot=(%.2f,%.2f,%.2f) pos=(%.3f,%.3f,%.3f)); nothing is being injected. "
              "The tracker's own numbers reach this point unshaped apart from the zoom "
              "scale, which was x%.4f on this frame, so a finite factor puts this on the "
              "tracker.",
              s.yaw, s.pitch, s.roll, s.pos_x, s.pos_y, s.pos_z, zoomFactor);
}

// Edge-triggered: "tracking did nothing" and "the mod thinks you are in a menu" are
// otherwise the same report, and a line per frame would bury both.
void ReportGameplayState(GameplayState state) {
    static GameplayState s_last = GameplayState::Playing;
    static bool s_reported = false;
    if (state == s_last && s_reported) {
        return;
    }
    s_last = state;
    s_reported = true;
    Log::Line("Gameplay: %s", Describe(state));
}

// One line, once, proving the pose reached the render view. A "no head tracking" report
// is otherwise ambiguous between a dead hook and a dead tracker, and the heartbeat can
// only tell those apart one level up.
void ReportFirstPoseOnce(const FrameSample& s) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("First tracked frame: rot=(%.2f,%.2f,%.2f) deg pos=(%.3f,%.3f,%.3f) m",
              s.yaw, s.pitch, s.roll, s.pos_x, s.pos_y, s.pos_z);
}

// Edge-triggered, on the same terms as the gameplay-state line: what the sights do to
// tracking is the first thing a player asks about, and a line per frame would bury it.
void LogAdsEdge(bool aiming, AdsMode mode) {
    static bool s_aiming = false;
    static AdsMode s_mode = kDefaultAdsMode;
    if (aiming == s_aiming && mode == s_mode) {
        return;
    }
    s_aiming = aiming;
    s_mode = mode;
    if (!aiming) {
        Log::Line("[ads] sights down - easing head tracking back to your head");
        return;
    }
    switch (mode) {
        case AdsMode::Paused:
            Log::Line("[ads] sights up - head tracking paused, view settling onto the aim");
            break;
        case AdsMode::Marker:
            Log::Line("[ads] sights up - view settling onto the aim, head tracking carries "
                      "on from there, stock crosshair correction enabled");
            break;
        case AdsMode::Tracked:
            Log::Line("[ads] sights up - view settling onto the aim, head tracking carries "
                      "on from there, stock crosshair correction disabled");
            break;
    }
}

// All three modes make the same swing onto the aim, and differ only in where the fade
// lands: `paused` runs the pose down to nothing and holds it there, the two tracked
// modes run it into the pose measured from the frame the sights came up on, which is
// identity at that moment and moves with the head from there. Roll is in neither fade, in
// any mode - see BlendAdsPose. Both the fade and the entry pose are asked in every mode,
// so the entry pose is dropped the moment the weapon comes down rather than one aim
// later.
FrameSample ShapeForAds(const FrameSample& s, bool aiming, AdsMode mode, float* scaleOut) {
    const float scale = g_adsFade.Update(aiming, NowMs());
    *scaleOut = scale;
    const AdsEntryPose::Pose absolute{ s.pitch, s.yaw, s.roll, s.pos_x, s.pos_y, s.pos_z };
    const AdsEntryPose::Pose relative = g_adsEntry.Relative(aiming, s.has_rotation,
                                                            absolute);
    const AdsEntryPose::Pose blended = BlendAdsPose(mode, scale, absolute, relative);

    FrameSample shaped = s;
    shaped.pitch = blended.pitch;
    shaped.yaw   = blended.yaw;
    shaped.roll  = blended.roll;
    shaped.pos_x = blended.x;
    shaped.pos_y = blended.y;
    shaped.pos_z = blended.z;
    return shaped;
}

void __fastcall Detour(void* thisptr, void* edx, UE3Vector* outLoc, UE3Rotator* outRot) {
    g_original(thisptr, edx, outLoc, outRot);
    g_totalCalls.fetch_add(1, std::memory_order_relaxed);

    // The accessor has 36 call sites, and the aim, trace, audio and interaction ones must
    // keep seeing the clean camera - that is the whole of the decoupling. Only the
    // scene-view caller gets the head pose, identified by the address it returns to.
    if (_ReturnAddress() != g_sceneViewCallSite) {
        return;
    }
    g_renderCalls.fetch_add(1, std::memory_order_relaxed);

    // The restore runs HERE, on the game thread, rather than from whichever thread asked
    // the mod to stand down. The widget position belongs to the game and this is the only
    // thread allowed to touch it; doing it from the init thread would interleave a
    // position update with this one.
    if (g_inert.load(std::memory_order_acquire)) {
        ShowGameCrosshairOnly();
        return;
    }

    TrackingRuntime* tracking = g_tracking.load(std::memory_order_acquire);
    if (!tracking || !outLoc || !outRot) {
        return;
    }

    // The main menu and the pause screen both build the scene view at the full frame
    // rate, so without this the head pose would keep swinging the camera while the player
    // reads a menu. Nothing is sampled outside gameplay either: the frame clock would
    // otherwise accumulate a menu's worth of time and hand the first frame back a delta
    // large enough to snap the view.
    const GameplayState state = GetGameplayState(thisptr);
    ReportGameplayState(state);
    const bool playing = state == GameplayState::Playing;
    const FrameSample s = playing ? tracking->SampleFrame() : FrameSample{};

    // The field of view the frame is about to be drawn with, and the unzoomed angle it is
    // a zoom against. Read on every GAMEPLAY frame rather than on every tracked one, and
    // that is the point rather than an accident: a factor that is wrong by a constant
    // reads exactly like a factor that is right, so the line that settles it must not
    // need a tracker connected to appear. It is used further down, on the frames a pose
    // actually reaches the camera.
    const ZoomScale zoom = playing ? ZoomScaleForFrame(thisptr, g_fovOverride)
                                   : ZoomScale{};

    // Polled every frame rather than latched off an enter/exit edge, and tested last in
    // the walk, so a menu still reports its own reason and leaves the sights flag false.
    // Only read once a camera is possessed: the zoom flags sit deep inside the gameplay
    // controller, and the front end is the one place this detour runs with something else
    // on the other end of the pointer.
    const bool aiming = playing && PlayerIsAiming(thisptr);
    const AdsMode adsMode = tracking->GetAdsMode();
    const TrackingState ts = DecideTracking(state, s.has_rotation || s.has_position,
                                            aiming, adsMode);
    if (!PoseApplies(ts.verdict)) {
        // A real suppression - menu, level transition, tracker gone, tracking switched
        // off. The transition and the pose the sights came up on are both dropped, so the
        // next aim re-enters cleanly instead of resuming against a pose from before it.
        g_adsFade.Reset();
        g_adsEntry.Reset();
        // Nothing injected this frame, so the rendered view IS the aim and the game's own
        // centred crosshair marks the shot.
        ShowGameCrosshairOnly();
        return;
    }
    ReportFirstPoseOnce(s);
    LogAdsEdge(ts.aiming, adsMode);

    // Tested here, before anything LATCHES the pose, rather than only inside ApplyHeadPose
    // at the end of the chain. AdsEntryPose banks the first aiming frame's pose as the
    // reference the whole aim is measured against, so one non-finite sample on the frame
    // the sights come up is stored and every later frame subtracts against it - tracking
    // dead for the rest of that aim in `marker` and `tracked`, with the one-shot warning
    // already spent on the first frame.
    if (!PoseIsFinite(s)) {
        ReportNonFiniteOnce(s, 1.0f);
        ShowGameCrosshairOnly();
        return;
    }

    // The fade's scale comes back out because the crosshair handover is decided on it,
    // not on the aiming edge - see ShouldDrawAimMarker.
    float adsScale = 1.0f;
    const FrameSample shaped = ShapeForAds(s, ts.aiming, adsMode, &adsScale);

    // The engine boundary, and the last thing done to the pose before it is written in.
    // The sights narrow the frame's field of view, which magnifies everything drawn in it
    // including the head pose, so the pose is shrunk by the same ratio and one head
    // degree stays worth the same amount of screen aimed or not. Exactly 1.0 whenever
    // nothing is zooming, so ordinary play is untouched.
    //
    // Applied after the ADS shaping rather than before it: the entry pose that `marker`
    // and `tracked` measure their relative motion against is in the tracker's own
    // degrees, and the field of view is still animating through the whole transition, so
    // scaling first would subtract two poses taken at two different factors.
    const FrameSample applied = ScaleForZoom(shaped, zoom.factor);

    const bool drawMarker =
        ShouldDrawAimMarker(g_showAimMarker, ts.aiming, adsMode, adsScale);

    const UE3Rotator clean = *outRot;
    Lean lean;
    if (!ApplyHeadPose(tracking->IsWorldSpaceYaw(), applied, outLoc, outRot, &lean)) {
        ReportNonFiniteOnce(applied, zoom.factor);
    }
    // Published for the scene-view hook to place: this detour runs INSIDE CalcSceneView,
    // so the projection matrix the frame will be drawn with has not been written yet. The
    // reticle is drawn from it once that hook has it - see the tail of its detour.
    const AimComponents aim = PublishAimMarker(clean, *outRot, drawMarker);

    const bool canPlaceMark =
        drawMarker && GetAimMarker().projection_valid.load(std::memory_order_acquire);
    ReportNoProjectionOnce(drawMarker && !canPlaceMark);
    // Stashed rather than reported here. The screen position is derived from the
    // half-field tangents, and this detour runs INSIDE CalcSceneView, so this frame's are
    // not published yet - reporting now would describe the mark with the PREVIOUS frame's
    // projection, which is wrong exactly while the field of view is animating, the case
    // the diagnostic exists for. The scene-view hook reports it once it has them.
    StashAimGeometry(applied, aim, lean);
    g_frameFresh.store(true, std::memory_order_release);
}

}  // namespace

bool InstallCameraHook(const CameraHookTargets& targets, TrackingRuntime& tracking,
                       const Config& cfg) {
    g_tracking.store(&tracking, std::memory_order_release);
    g_showAimMarker = cfg.show_aim_marker;
    g_aimGeometryLog = cfg.aim_geometry_log;
    g_fovOverride = cfg.fov_override;
    g_sceneViewCallSite = reinterpret_cast<void*>(targets.sceneViewCallSite);

    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        Log::Line("ERROR: MH_Initialize failed: %d", init);
        return false;
    }

    g_target = reinterpret_cast<void*>(targets.getPlayerViewPoint);
    const MH_STATUS st = CreateAndEnableHook(g_target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking APlayerController::GetPlayerViewPoint @ 0x%p failed: %d",
                  g_target, st);
        g_target = nullptr;
        g_original = nullptr;
        return false;
    }

    Log::Line("Camera hook installed: APlayerController::GetPlayerViewPoint @ 0x%p, "
              "scene-view call site 0x%p", g_target, g_sceneViewCallSite);
    return true;
}

void DisableCameraTracking() {
    // The detour is left INSTALLED, deliberately, and this is the whole reason the
    // function is a flag rather than an unhook.
    //
    // Removing a detour the game is actively calling cannot be made safe from here. The
    // detour's first statement calls through g_original, and a thread can be one
    // instruction from it when the removal starts. MinHook suspends threads and relocates
    // instruction pointers that sit inside its own trampoline, but it has no visibility
    // into this mod's detour body, so a thread already past that point still calls the
    // trampoline - and MH_Uninitialize then frees the page it lives on. The game gets an
    // access violation on a path taken 36 times per frame.
    //
    // An installed detour that reads one atomic and returns costs a predictable nothing.
    // Unhooking one under a running game costs the player a crash, on the path whose
    // entire purpose is to fail safely.
    // Inert first. The detour tests `g_tracking` before it tests nothing else, so a frame
    // landing between the two stores in the other order takes the null-runtime early
    // return, which does not hand the game's crosshair back, and it stays displaced.
    g_inert.store(true, std::memory_order_release);
    g_tracking.store(nullptr, std::memory_order_release);
}

unsigned long CameraHookCallCount() {
    return g_renderCalls.load(std::memory_order_relaxed);
}

unsigned long CameraHookNonRenderCallCount() {
    // Render first, then total. The detour increments total before render, so total is
    // never behind render at any instant, and reading render first can only make it
    // smaller - the subtraction cannot wrap. The other order can, and an unsigned wrap
    // reads as a huge non-render count, which is exactly the condition the heartbeat
    // turns into a "the rendered view is not being tracked" warning.
    const unsigned long render = g_renderCalls.load(std::memory_order_relaxed);
    const unsigned long total = g_totalCalls.load(std::memory_order_relaxed);
    return total - render;
}

bool ConsumeCameraFrame() {
    return g_frameFresh.exchange(false, std::memory_order_acq_rel);
}

void ReportAimGeometry() {
    if (!g_aimGeometryLog) {
        return;
    }
    static unsigned long long lastMs = 0;
    const unsigned long long now = NowMs();
    if (now - lastMs < kAimGeometryIntervalMs) {
        return;
    }
    lastMs = now;

    // Read here rather than in the detour: the half-field tangents this divides by are
    // published by the scene-view hook, so asking any earlier describes the mark with the
    // previous frame's projection.
    float ndcX = 0.0f, ndcY = 0.0f;
    const AimProjection result = ProjectAim(&ndcX, &ndcY);
    const AimGeometrySample& g = g_aimGeometry;
    Log::Line("[aimgeo] head=(%.2f,%.2f,%.2f)deg aim_ruf=(%.4f,%.4f,%.4f) "
              "ndc=(%.4f,%.4f) %s lean_ruf=(%.1f,%.1f,%.1f)cm",
              g.pose.yaw, g.pose.pitch, g.pose.roll, g.aim.right, g.aim.up, g.aim.forward,
              ndcX, ndcY, Describe(result), g.lean.ruf[0], g.lean.ruf[1], g.lean.ruf[2]);
}

}  // namespace BioShockInfiniteHeadTracking
