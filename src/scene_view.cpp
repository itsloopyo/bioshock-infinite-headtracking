#include "scene_view.h"

#include "aim_marker.h"
#include "fov_override.h"
#include "logging.h"
#include "camera_hook.h"
#include "game_crosshair.h"
#include "memory_probe.h"
#include "minhook_util.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {

namespace {

// ULocalPlayer::CalcSceneView(FSceneViewFamily*, FVector& out_ViewLocation,
// FRotator& out_ViewRotation, FViewport*, FViewElementDrawer*) - ends `ret 0x14`, so
// five stack arguments and `this` in ecx, modelled as __fastcall with a dummy edx.
// Returns the FSceneView the frame is rendered from.
using CalcSceneView_t = void*(__fastcall*)(void* thisptr, void* edx, void* family,
                                           void* outLocation, void* outRotation,
                                           void* viewport, void* viewDrawer);

CalcSceneView_t g_original = nullptr;
void*           g_target = nullptr;
std::atomic<unsigned long> g_calls{0};

// Byte offset of the projection matrix inside the returned FSceneView, found once by
// shape and reused. -1 until then.
std::atomic<int> g_projectionOffset{-1};

// How far into the FSceneView the search looks. The matrices sit near the front of the
// object in every UE3 layout, and a window this size costs a few hundred float
// comparisons on the one frame that runs it.
constexpr int kSearchBytes = 0x400;

// A 4x4 float matrix: what the scan tests at each candidate offset, and how much room it
// needs past that offset to do so.
constexpr int kMatrixFloats = 16;
constexpr int kMatrixBytes = kMatrixFloats * static_cast<int>(sizeof(float));

// Bounds on a half-field tangent this game could have drawn with. 0.176 is a 20 degree
// full angle. The upper bound is deliberately not the mirror of it: the game holds its
// vertical field to a 16:9 reference and widens the horizontal one with the display's
// aspect ratio, so on the 3.5556 screen this was measured on the horizontal tangent is
// twice the 16:9 one, and a wide display at a wide [View] Fov reaches horizontal
// tangents no plain field-of-view angle would. 12 covers the widest configurable angle
// on a display three 16:9 panels across; a tighter bound would leave the matrix unfound
// on that machine and the reticle with nothing to project through.
constexpr float kMinTan = 0.176f;
constexpr float kMaxTan = 12.0f;

// The tangents are what the reticle divides by; the angles beside them in the log line
// are for the reader, so the conversion appears once here rather than at each of them.
constexpr float kRadToDeg = 57.2957795f;

bool IsZero(float v) {
    return v == 0.0f;
}

// A UE3 perspective projection, in row-vector form:
//
//   [ 1/tanH    0       0     0 ]
//   [   0     1/tanV    0     0 ]
//   [   j       k       c     1 ]
//   [   0       0       d     0 ]
//
// Row 2's first two entries are left free because a temporal-antialiasing jitter is
// written there. Row 2 column 3 being exactly 1 with row 3 column 3 exactly 0 is what
// makes this a perspective divide by view depth rather than any other matrix in the
// object.
bool LooksLikeProjection(const float* m) {
    for (int i = 0; i < kMatrixFloats; ++i) {
        if (!std::isfinite(m[i])) {
            return false;
        }
    }
    if (!IsZero(m[1]) || !IsZero(m[2]) || !IsZero(m[3])) return false;
    if (!IsZero(m[4]) || !IsZero(m[6]) || !IsZero(m[7])) return false;
    if (m[11] != 1.0f) return false;
    if (!IsZero(m[12]) || !IsZero(m[13]) || !IsZero(m[15])) return false;
    if (m[0] <= 0.0f || m[5] <= 0.0f) return false;
    const float tanH = 1.0f / m[0];
    const float tanV = 1.0f / m[5];
    return tanH >= kMinTan && tanH <= kMaxTan && tanV >= kMinTan && tanV <= kMaxTan;
}

// Reported with every term the reticle depends on, so a placement argument can be
// settled against the numbers the renderer used instead of against an assumed
// field-of-view convention.
//
// Twice per session at most, and never on a frame in between: once for a frame the game
// chose the angle for, and once for a frame the [View] Fov override chose it for. The
// second line is how the override is known to have reached the renderer - it is the same
// matrix the reticle is placed with, so an angle that moved here moved for the reticle
// too, and one that did not says the override never got as far as the projection.
void ReportProjection(int offset, const float* m) {
    static bool reportedGame = false;
    static bool reportedOverridden = false;
    const bool overridden = FovOverrideActive();
    bool& latch = overridden ? reportedOverridden : reportedGame;
    if (latch) {
        return;
    }
    latch = true;
    const float tanH = 1.0f / m[0];
    const float tanV = 1.0f / m[5];
    const float degH = 2.0f * std::atan(tanH) * kRadToDeg;
    const float degV = 2.0f * std::atan(tanV) * kRadToDeg;
    Log::Line("Scene view projection at FSceneView+0x%X (%s field of view): tanH=%.5f "
              "tanV=%.5f (%.1f x %.1f degrees, aspect %.4f), M[2][2]=%.6f M[3][2]=%.4f",
              offset, overridden ? "overridden" : "the game's", tanH, tanV, degH, degV,
              tanH / tanV, m[10], m[14]);
}

// Frames the scan may come up empty on before it says so. The first few frames of a
// level are drawn before the view family is fully built, so one miss means nothing; a
// second of them means the matrix is not where this looks or is not the shape this
// recognises, and the player is owed the reason rather than a reticle that never appears.
constexpr unsigned long kScanFailuresBeforeReport = 120;
unsigned long g_scanFailures = 0;

// Said once, and only from the detour, so it is on the game thread with everything else
// here. Names the window and the bounds because those are the two things that have to
// change to find it on a build the scan does not fit.
void ReportScanFailureOnce() {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("WARN: no projection matrix found in the first 0x%X bytes of FSceneView "
              "after %lu frames (looking for a perspective row-vector matrix whose "
              "half-field tangents both fall in %.3f..%.1f). The aim marker will not be "
              "drawn - head tracking itself is unaffected.",
              kSearchBytes, kScanFailuresBeforeReport, kMinTan, kMaxTan);
}

void ScanForProjection(const std::uint8_t* view) {
    // kSearchBytes is a span this side CHOSE - nothing here knows how big an FSceneView
    // is - so it is asked about before it is read, unlike an address a build profile
    // supplied. One query on the first frame that scans, against a struct the engine has
    // just handed us on a heap its other threads are still working.
    if (!IsReadable(view, static_cast<std::size_t>(kSearchBytes))) {
        // Counted, not just refused. A silent return here leaves the scan permanently off
        // with nothing in the log - the same "no mark and no reason" the failure counter
        // exists to prevent - and this branch can latch: the span must sit inside one
        // region, so an FSceneView near a region end refuses on every frame forever.
        if (++g_scanFailures >= kScanFailuresBeforeReport) {
            ReportScanFailureOnce();
        }
        return;
    }
    for (int offset = 0; offset + kMatrixBytes <= kSearchBytes; offset += 4) {
        const float* m = reinterpret_cast<const float*>(view + offset);
        if (!LooksLikeProjection(m)) {
            continue;
        }
        g_projectionOffset.store(offset, std::memory_order_relaxed);
        return;
    }
    if (++g_scanFailures >= kScanFailuresBeforeReport) {
        ReportScanFailureOnce();
    }
}

void PublishProjection(const float* m) {
    AimMarker& marker = GetAimMarker();
    // The tangents are stored rather than the matrix entries because that is what the
    // projection divides by, and because a zero entry would reach the reticle as an
    // infinity if it were inverted there instead.
    if (m[0] <= 0.0f || m[5] <= 0.0f || !std::isfinite(m[0]) || !std::isfinite(m[5])) {
        marker.projection_valid.store(false, std::memory_order_relaxed);
        return;
    }
    marker.tan_half_h.store(1.0f / m[0], std::memory_order_relaxed);
    marker.tan_half_v.store(1.0f / m[5], std::memory_order_relaxed);
    marker.projection_valid.store(true, std::memory_order_release);
}

void* __fastcall Detour(void* thisptr, void* edx, void* family, void* outLocation,
                        void* outRotation, void* viewport, void* viewDrawer) {
    void* view = g_original(thisptr, edx, family, outLocation, outRotation, viewport,
                            viewDrawer);
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (!view) {
        return view;
    }

    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(view);
    int offset = g_projectionOffset.load(std::memory_order_relaxed);
    if (offset < 0) {
        ScanForProjection(bytes);
        offset = g_projectionOffset.load(std::memory_order_relaxed);
    }
    if (offset >= 0) {
        const float* m = reinterpret_cast<const float*>(bytes + offset);
        PublishProjection(m);
        ReportProjection(offset, m);
    }

    // The camera hook runs inside CalcSceneView, before this frame's projection exists.
    if (ConsumeCameraFrame()) {
        PositionGameCrosshair();
        ReportAimGeometry();
    }
    return view;
}

}  // namespace

bool InstallSceneViewHook(std::uintptr_t calcSceneViewAddr) {
    g_target = reinterpret_cast<void*>(calcSceneViewAddr);
    const MH_STATUS st = CreateAndEnableHook(g_target, reinterpret_cast<void*>(&Detour),
                                             reinterpret_cast<void**>(&g_original));
    if (st != MH_OK) {
        Log::Line("ERROR: hooking ULocalPlayer::CalcSceneView @ 0x%p failed: %d",
                  g_target, st);
        g_target = nullptr;
        g_original = nullptr;
        return false;
    }
    Log::Line("Scene view hook installed: ULocalPlayer::CalcSceneView @ 0x%p", g_target);
    return true;
}

int SceneViewProjectionOffset() {
    return g_projectionOffset.load(std::memory_order_relaxed);
}

unsigned long SceneViewCallCount() {
    return g_calls.load(std::memory_order_relaxed);
}

}  // namespace BioShockInfiniteHeadTracking
