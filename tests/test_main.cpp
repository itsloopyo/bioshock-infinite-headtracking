// Characterization tests for the parts of the mod that are pure arithmetic.
//
// Everything else in this mod needs BioShock Infinite running to mean anything: the
// hooks, the offsets and the build profile are all statements about another process's
// memory. What is left over is the logic every rendered frame passes through, and it is
// worth pinning precisely because a sign or an ordering error in it produces a camera
// that points somewhere plausible - the picture moves, it just moves wrongly - which is
// the failure mode that gets through a play test.
//
// Deliberately no test framework, and no Windows headers: this has to build anywhere the
// maths does. Until this repo has a build system, run it with
//
//   g++ -std=c++17 -Isrc -Icameraunlock-core/cpp/include tests/test_main.cpp -o tests.exe
//
// from the repo root, then run tests.exe. Exit code 0 means every check passed.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "ads.h"
#include "ads_gate.h"
#include "config.h"
#include "hotkeys.h"
#include "fov_override.h"
#include "head_pose.h"
#include "hud_projection.h"
#include "ue3_rotation.h"
#include "ue3_types.h"
#include "window_placement.h"
#include "zoom_scale.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const char* file, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s\n", file, line, what);
}

void CheckNear(float got, float want, float tol, const char* what, const char* file,
               int line) {
    ++g_checks;
    if (std::isfinite(got) && std::fabs(got - want) <= tol) return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s: got %.6f, want %.6f (tol %.6f)\n",
                file, line, what, got, want, tol);
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)
#define CHECK_NEAR(got, want, tol) CheckNear((got), (want), (tol), #got, __FILE__, __LINE__)

using namespace BioShockInfiniteHeadTracking;

float ToRad(float deg) {
    return deg * 3.14159265f / 180.0f;
}

AdsEntryPose::Pose MakePose(float pitch, float yaw, float roll,
                            float x = 0.0f, float y = 0.0f, float z = 0.0f) {
    AdsEntryPose::Pose p;
    p.pitch = pitch;
    p.yaw = yaw;
    p.roll = roll;
    p.x = x;
    p.y = y;
    p.z = z;
    return p;
}

// ---- ads config ------------------------------------------------------------
//
// The setting is a cross-mod contract: the same key, the same three strings and the same
// default in every shooter, so a player who has cycled it in one finds it here.

void TestAdsConfigDefaultsToPaused() {
    Config c;
    CHECK(c.ads_mode == AdsMode::Paused);
    CHECK(defaults::kAdsMode == AdsMode::Paused);
}

void TestAdsConfigBindsInsertAndTheChord() {
    CHECK(defaults::kVkAdsMode == 0x2D);  // VK_INSERT
    // The 4th nav-cluster slot is its own binding, not one of the other three moved over.
    CHECK(defaults::kVkToggle == 0x23);
    CHECK(defaults::kVkCycleMode == 0x21);
    CHECK(defaults::kVkYawMode == 0x22);
    CHECK(defaults::kVkAdsMode != defaults::kVkToggle);
    CHECK(defaults::kVkAdsMode != defaults::kVkCycleMode);
    CHECK(defaults::kVkAdsMode != defaults::kVkYawMode);
}

// An unknown value is the DEFAULT, not whichever branch happens to be last. That covers
// a typo in a hand-edited file, and it is the migration path for a mode renamed since an
// older release wrote the key.
void TestAdsConfigUnknownValueFallsBackToTheDefault() {
    CHECK(ParseAdsMode("paused") == AdsMode::Paused);
    CHECK(ParseAdsMode("marker") == AdsMode::Marker);
    CHECK(ParseAdsMode("tracked") == AdsMode::Tracked);
    CHECK(ParseAdsMode("  TRACKED\r\n") == AdsMode::Tracked);
    CHECK(ParseAdsMode("trackd") == kDefaultAdsMode);
    CHECK(ParseAdsMode("") == kDefaultAdsMode);
    CHECK(ParseAdsMode(nullptr) == kDefaultAdsMode);
}

// Three slots, in the order the README and the INI comment both state.
void TestAdsConfigCycleOrder() {
    CHECK(NextAdsMode(AdsMode::Paused) == AdsMode::Marker);
    CHECK(NextAdsMode(AdsMode::Marker) == AdsMode::Tracked);
    CHECK(NextAdsMode(AdsMode::Tracked) == AdsMode::Paused);
}

// ---- ads entry pose (cameraunlock/ads/entry_pose.h) ------------------------
//
// The entry pose is what makes the tracked ADS modes swing onto the aim and then keep
// tracking from there. The seam and the capture timing are invisible from a settings or
// a gate test, and both are easy to regress.

void TestAdsPoseHipPassesThrough() {
    AdsEntryPose entry;
    const auto out = entry.Relative(false, true, MakePose(5.0f, 10.0f, 2.0f, 0.1f, 0.2f, 0.3f));
    CHECK_NEAR(out.pitch, 5.0f, 1e-5f);
    CHECK_NEAR(out.yaw, 10.0f, 1e-5f);
    CHECK_NEAR(out.roll, 2.0f, 1e-5f);
    CHECK_NEAR(out.x, 0.1f, 1e-5f);
    CHECK(!entry.HasEntry());
}

// The frame the sights come up on is identity, which is what makes the swing onto the aim
// the same one `paused` makes.
void TestAdsPoseEntryFrameIsIdentity() {
    AdsEntryPose entry;
    const auto out = entry.Relative(true, true, MakePose(5.0f, 10.0f, 2.0f, 0.1f, 0.2f, 0.3f));
    CHECK_NEAR(out.pitch, 0.0f, 1e-5f);
    CHECK_NEAR(out.yaw, 0.0f, 1e-5f);
    CHECK_NEAR(out.x, 0.0f, 1e-5f);
    CHECK_NEAR(out.y, 0.0f, 1e-5f);
    CHECK_NEAR(out.z, 0.0f, 1e-5f);
    CHECK(entry.HasEntry());
}

// Roll moves no aim point, so zeroing it would yank a head tilt the player is actively
// holding back to level and lean it in again as they move.
void TestAdsPoseRollStaysAbsolute() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 0.0f, 7.0f));
    const auto out = entry.Relative(true, true, MakePose(0.0f, 0.0f, 9.0f));
    CHECK_NEAR(out.roll, 9.0f, 1e-5f);
}

// Yaw arrives wrapped into -180..180, so a plain subtraction reads a 10 degree move
// across the seam as -350 and whips the view a full turn the wrong way.
void TestAdsPoseYawCrossesTheSeamTheShortWay() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 175.0f, 0.0f));
    const auto out = entry.Relative(true, true, MakePose(0.0f, -175.0f, 0.0f));
    CHECK_NEAR(out.yaw, 10.0f, 1e-4f);

    AdsEntryPose back;
    back.Relative(true, true, MakePose(0.0f, -175.0f, 0.0f));
    const auto other = back.Relative(true, true, MakePose(0.0f, 175.0f, 0.0f));
    CHECK_NEAR(other.yaw, -10.0f, 1e-4f);
}

void TestAdsPosePitchAndPositionAreRelative() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(5.0f, 0.0f, 0.0f, 0.10f, 0.20f, 0.30f));
    const auto out = entry.Relative(true, true, MakePose(8.0f, 0.0f, 0.0f, 0.15f, 0.18f, 0.35f));
    CHECK_NEAR(out.pitch, 3.0f, 1e-5f);
    CHECK_NEAR(out.x, 0.05f, 1e-5f);
    CHECK_NEAR(out.y, -0.02f, 1e-5f);
    CHECK_NEAR(out.z, 0.05f, 1e-5f);
}

// Interpolators publish nothing on a suppressed frame. Capturing then would freeze a
// pre-suppression pose and hold the whole aim at that offset. The path that hits it:
// aim, open a menu, move your head, come back with the sights still up.
void TestAdsPoseCaptureWaitsForALiveRotation() {
    AdsEntryPose entry;
    const auto dead = entry.Relative(true, false, MakePose(4.0f, 6.0f, 0.0f));
    CHECK(!entry.HasEntry());
    CHECK_NEAR(dead.yaw, 6.0f, 1e-5f);

    const auto live = entry.Relative(true, true, MakePose(9.0f, 11.0f, 0.0f));
    CHECK(entry.HasEntry());
    CHECK_NEAR(live.yaw, 0.0f, 1e-5f);
    CHECK_NEAR(live.pitch, 0.0f, 1e-5f);
}

void TestAdsPoseLoweringTheWeaponDropsTheEntry() {
    AdsEntryPose entry;
    entry.Relative(true, true, MakePose(0.0f, 30.0f, 0.0f));
    CHECK(entry.HasEntry());
    const auto down = entry.Relative(false, true, MakePose(0.0f, 30.0f, 0.0f));
    CHECK(!entry.HasEntry());
    CHECK_NEAR(down.yaw, 30.0f, 1e-5f);
}

// ---- ads blend (cameraunlock/ads/ads_blend.h) ------------------------------

void TestAdsBlendHipIsTheHeadPose() {
    const auto absolute = MakePose(5.0f, 10.0f, 2.0f, 0.1f, 0.2f, 0.3f);
    const auto relative = MakePose(0.0f, 0.0f, 2.0f);
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        const auto out = BlendAdsPose(mode, 1.0f, absolute, relative);
        CHECK_NEAR(out.pitch, 5.0f, 1e-5f);
        CHECK_NEAR(out.yaw, 10.0f, 1e-5f);
        CHECK_NEAR(out.roll, 2.0f, 1e-5f);
        CHECK_NEAR(out.z, 0.3f, 1e-5f);
    }
}

// With the sights up in `paused` the frame is the game's own, bar the head tilt: roll
// moves neither the eye off the barrel nor the aim off the middle of the frame.
void TestAdsBlendPausedKeepsRollAndDropsTheRest() {
    const auto absolute = MakePose(5.0f, 10.0f, 3.0f, 0.1f, 0.2f, 0.3f);
    const auto out = BlendAdsPose(AdsMode::Paused, 0.0f, absolute, MakePose(0, 0, 3.0f));
    CHECK_NEAR(out.pitch, 0.0f, 1e-5f);
    CHECK_NEAR(out.yaw, 0.0f, 1e-5f);
    CHECK_NEAR(out.x, 0.0f, 1e-5f);
    CHECK_NEAR(out.y, 0.0f, 1e-5f);
    CHECK_NEAR(out.z, 0.0f, 1e-5f);
    CHECK_NEAR(out.roll, 3.0f, 1e-5f);
}

void TestAdsBlendTrackedLandsOnTheEntryRelativePose() {
    const auto absolute = MakePose(5.0f, 10.0f, 3.0f, 0.1f, 0.2f, 0.3f);
    const auto relative = MakePose(1.0f, 2.0f, 3.0f, 0.01f, 0.02f, 0.03f);
    for (const AdsMode mode : { AdsMode::Marker, AdsMode::Tracked }) {
        const auto out = BlendAdsPose(mode, 0.0f, absolute, relative);
        CHECK_NEAR(out.pitch, 1.0f, 1e-5f);
        CHECK_NEAR(out.yaw, 2.0f, 1e-5f);
        CHECK_NEAR(out.roll, 3.0f, 1e-5f);
        CHECK_NEAR(out.x, 0.01f, 1e-5f);
        CHECK_NEAR(out.z, 0.03f, 1e-5f);
    }
}

// ---- ads fade (cameraunlock/ads/ads_fade.h) --------------------------------
//
// The one case a held aim never reaches: a tap of the aim button. A leg that starts at
// its own endpoint rather than where the transition actually is removes a fully applied
// pose in one frame, which is the jolt the fade exists to prevent.
void TestAdsFadeReversalStartsFromWhereItIs() {
    AdsFade fade;
    CHECK_NEAR(fade.Update(false, 1000), 1.0f, 1e-5f);
    // The press itself starts the leg at 1.0; the fade is only partway through a frame
    // later, which is where a tap releases.
    CHECK_NEAR(fade.Update(true, 1000), 1.0f, 1e-5f);
    const float partway = fade.Update(true, 1000 + AdsFade::kLowerMs / 3);
    CHECK(partway < 1.0f && partway > 0.0f);
    const float reversed = fade.Update(false, 1000 + AdsFade::kLowerMs / 3);
    CHECK_NEAR(reversed, partway, 1e-5f);
}

void TestAdsFadeSuppressionResetsToTheHip() {
    AdsFade fade;
    fade.Update(true, 0);
    CHECK_NEAR(fade.Update(true, AdsFade::kLowerMs * 2), 0.0f, 1e-5f);
    fade.Reset();
    CHECK_NEAR(fade.Update(true, AdsFade::kLowerMs * 3), 1.0f, 1e-5f);
}

// ---- ads_gate --------------------------------------------------------------
//
// The verdict walk. ADS is tested last so a menu still reports its own reason when both
// are true at once.

void TestGatePausedSuspendsAndStillReportsTheSights() {
    const auto s = DecideTracking(GameplayState::Playing, true, true, AdsMode::Paused);
    CHECK(s.verdict == TrackingVerdict::AdsSuspended);
    // The gate says whether tracking applies; this says what the weapon is doing, and the
    // per-frame code needs both.
    CHECK(s.aiming);
    // The pose keeps flowing: suspending is an ease-out, not a switch.
    CHECK(PoseApplies(s.verdict));
}

void TestGateTrackedModesKeepTheGateOpen() {
    for (const AdsMode mode : { AdsMode::Marker, AdsMode::Tracked }) {
        const auto s = DecideTracking(GameplayState::Playing, true, true, mode);
        CHECK(s.verdict == TrackingVerdict::Active);
        CHECK(s.aiming);
        CHECK(PoseApplies(s.verdict));
    }
}

void TestGateHipFireIsActiveInEveryMode() {
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        const auto s = DecideTracking(GameplayState::Playing, true, false, mode);
        CHECK(s.verdict == TrackingVerdict::Active);
        CHECK(!s.aiming);
    }
}

// A menu or a level transition outranks ADS in the reported reason, and clears the flag:
// a stale one through a menu would keep the marker running against a weapon that is not
// raised.
void TestGateSuppressionOutranksAdsAndClearsTheFlag() {
    const struct { GameplayState state; TrackingVerdict verdict; } cases[] = {
        { GameplayState::NoCamera, TrackingVerdict::NoCamera },
        { GameplayState::Menu,     TrackingVerdict::GamePaused },
        { GameplayState::Paused,   TrackingVerdict::GamePaused },
    };
    for (const auto& c : cases) {
        for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
            const auto s = DecideTracking(c.state, true, true, mode);
            CHECK(s.verdict == c.verdict);
            CHECK(!s.aiming);
            CHECK(!PoseApplies(s.verdict));
        }
    }
}

// No tracker is not an ADS verdict either, and it must not report the sights.
void TestGateNoTrackerOutranksAds() {
    const auto s = DecideTracking(GameplayState::Playing, false, true, AdsMode::Tracked);
    CHECK(s.verdict == TrackingVerdict::NoTracker);
    CHECK(!s.aiming);
    CHECK(!PoseApplies(s.verdict));
}

// The state is polled, so it heals on the next frame without an exit edge ever arriving -
// which is what a state machine that transitions without firing one leaves us with.
void TestGateAdsStateHealsWithoutAnExitEdge() {
    const auto aimed = DecideTracking(GameplayState::Playing, true, true, AdsMode::Paused);
    CHECK(aimed.verdict == TrackingVerdict::AdsSuspended);
    const auto healed = DecideTracking(GameplayState::Playing, true, false, AdsMode::Paused);
    CHECK(healed.verdict == TrackingVerdict::Active);
    CHECK(!healed.aiming);
}


// ---- aim marker gate (ads_gate.h) ------------------------------------------
//
// [General] ShowAimMarker is the player's answer for the whole session, so it outranks
// the ADS mode: with it off, `marker` mode must not put a second crosshair back on
// screen. The setting was read, logged and documented while the detour decided from the
// ADS mode alone, so turning it off changed nothing.

void TestAimMarkerOffSuppressesEveryMode() {
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        for (const bool aiming : { true, false }) {
            CHECK(!ShouldDrawAimMarker(false, aiming, mode, 0.0f));
            CHECK(!ShouldDrawAimMarker(false, aiming, mode, 1.0f));
        }
    }
}

// At the hip the mark is drawn in every mode: the game's crosshair is fixed to the middle
// of the screen and the head has moved the view off it.
void TestAimMarkerOnDrawsAtTheHipInEveryMode() {
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        CHECK(ShouldDrawAimMarker(true, false, mode, 1.0f));
    }
}

// With the sights up it is `marker` mode alone. `tracked` is the same mode with a cleaner
// screen, and in `paused` the view settles onto the aim, which is where the game's own
// crosshair already is.
void TestAimMarkerWithSightsUpIsMarkerModeOnly() {
    CHECK(ShouldDrawAimMarker(true, true, AdsMode::Marker, 0.0f));
    CHECK(!ShouldDrawAimMarker(true, true, AdsMode::Paused, 0.0f));
    CHECK(!ShouldDrawAimMarker(true, true, AdsMode::Tracked, 0.0f));
}

// The handover in `paused` waits for the pose to have actually gone, not for the aiming
// edge. Suspending is a 150 ms ease, so deciding on the edge put the game's centred
// crosshair back while the head pose was still almost entirely applied - nine frames at
// 60 Hz of a crosshair in the middle of a view swung off the aim by the whole head angle.
void TestAimMarkerHoldsInPausedUntilTheFadeHasLanded() {
    // Mid-ease: the pose is still going in, so the mark stays.
    CHECK(ShouldDrawAimMarker(true, true, AdsMode::Paused, 1.0f));
    CHECK(ShouldDrawAimMarker(true, true, AdsMode::Paused, 0.5f));
    CHECK(ShouldDrawAimMarker(true, true, AdsMode::Paused, 0.01f));
    // Landed: the view is on the aim and the game's own crosshair marks the shot.
    CHECK(!ShouldDrawAimMarker(true, true, AdsMode::Paused, 0.0f));
    // `tracked` keeps a clean screen throughout the ease, and the player's own
    // ShowAimMarker=false still outranks every one of these.
    CHECK(!ShouldDrawAimMarker(true, true, AdsMode::Tracked, 1.0f));
    CHECK(!ShouldDrawAimMarker(false, true, AdsMode::Paused, 1.0f));
}

// ---- field of view override (fov_override.h) -------------------------------
//
// The override is a ratio against the angle the camera renders at when nothing is
// zooming, which is the whole reason iron sights and scopes still zoom under it. Written
// as a value over the top of the frame's angle instead, it flattens every one of them,
// and that is invisible until someone raises a scope in game.

void TestFovOffLeavesTheGameAngleAlone() {
    for (const float requested : { 0.0f, -1.0f }) {
        const auto d = DecideFov(requested, 70.0f, 70.0f);
        CHECK(d.status == FovOverrideStatus::Off);
        CHECK_NEAR(d.fov, 70.0f, 1e-4f);
    }
    CHECK(defaults::kFovOverride == 0.0f);
}

// An unzoomed frame renders at exactly the configured number, which is what the INI
// comment and the README both promise.
void TestFovUnzoomedFrameRendersTheConfiguredAngle() {
    const auto d = DecideFov(100.0f, 70.0f, 70.0f);
    CHECK(d.status == FovOverrideStatus::Applied);
    CHECK_NEAR(d.fov, 100.0f, 1e-3f);
}

// A scope at half the base angle stays at half of the widened one, so the zoom is worth
// the same factor it was before the override.
void TestFovZoomKeepsItsFactor() {
    const auto zoomed = DecideFov(100.0f, 35.0f, 70.0f);
    CHECK(zoomed.status == FovOverrideStatus::Applied);
    CHECK_NEAR(zoomed.fov, 50.0f, 1e-3f);
    const auto open = DecideFov(100.0f, 70.0f, 70.0f);
    CHECK_NEAR(zoomed.fov / open.fov, 35.0f / 70.0f, 1e-4f);
}

// A base angle that is not an angle means the offset no longer fits the build. The frame
// keeps the game's own angle rather than being scaled by a garbage number.
void TestFovRefusesAnImpossibleBase() {
    for (const float base : { 0.0f, -70.0f, 500.0f, 1e-30f }) {
        const auto d = DecideFov(100.0f, 70.0f, base);
        CHECK(d.status == FovOverrideStatus::NoBaseAngle);
        CHECK_NEAR(d.fov, 70.0f, 1e-4f);
    }
}

// Past 180 degrees there is no perspective projection left. That is one frame's refusal,
// not the override switching itself off, so the next normal frame is widened again.
void TestFovRefusesAFrameWithNoProjection() {
    const auto d = DecideFov(150.0f, 120.0f, 40.0f);
    CHECK(d.status == FovOverrideStatus::NotRenderable);
    CHECK_NEAR(d.fov, 120.0f, 1e-4f);
    const auto next = DecideFov(150.0f, 40.0f, 40.0f);
    CHECK(next.status == FovOverrideStatus::Applied);
}

// ---- zoom compensation (zoom_scale.h) --------------------------------------
//
// A narrow field of view magnifies everything in the frame, head tracking included, so
// the pose is shrunk until it displaces the picture by as much as it would have at the
// unzoomed angle. The failure this pins is silent: a factor that is wrong by a constant
// looks exactly like a factor that is right, and the only symptom is head tracking
// feeling weak, or strong, everywhere.

// The gate. Nothing zooming means the two angles are the same angle, and the pose has to
// come out the other side untouched.
void TestZoomIsExactlyOneWhenNothingZooms() {
    const ZoomScale z = DecideZoomScale(82.5f, 82.5f, 0.0f);
    CHECK(z.known);
    CHECK_NEAR(z.factor, 1.0f, 1e-6f);

    FrameSample s;
    s.has_rotation = true;
    s.yaw = 12.0f; s.pitch = -7.0f; s.roll = 5.0f;
    s.has_position = true;
    s.pos_x = 0.05f; s.pos_y = -0.02f; s.pos_z = 0.11f;
    const FrameSample out = ScaleForZoom(s, z.factor);
    CHECK_NEAR(out.yaw, s.yaw, 1e-4f);
    CHECK_NEAR(out.pitch, s.pitch, 1e-4f);
    CHECK_NEAR(out.pos_x, s.pos_x, 1e-6f);
    CHECK_NEAR(out.pos_z, s.pos_z, 1e-6f);
}

// The factor is the ratio of the half-angle tangents, not of the angles. A weapon that
// halves the ANGLE does not halve the picture's scale, and using the angles would leave
// the compensation wrong by the difference.
void TestZoomIsTheTangentRatioNotTheAngleRatio() {
    const ZoomScale z = DecideZoomScale(45.0f, 90.0f, 0.0f);
    CHECK(z.known);
    const float want = std::tan(22.5f * 3.14159265f / 180.0f)
                     / std::tan(45.0f * 3.14159265f / 180.0f);
    CHECK_NEAR(z.factor, want, 1e-5f);
    CHECK(z.factor < 1.0f);
    // 0.5 is what the ratio of the angles would have given, and it is a long way off.
    CHECK(std::fabs(z.factor - 0.5f) > 0.08f);
}

// Yaw, pitch and the lean shrink with the zoom; roll does not. Ten degrees of head roll
// rolls the picture ten degrees at every field of view there is.
void TestZoomScalesYawPitchAndPositionButNotRoll() {
    FrameSample s;
    s.has_rotation = true;
    s.yaw = 10.0f; s.pitch = 6.0f; s.roll = 8.0f;
    s.has_position = true;
    s.pos_x = 0.20f; s.pos_y = 0.10f; s.pos_z = -0.30f;

    const float factor = 0.5f;
    const FrameSample out = ScaleForZoom(s, factor);

    CHECK(out.roll == s.roll);
    CHECK_NEAR(out.pos_x, 0.10f, 1e-6f);
    CHECK_NEAR(out.pos_y, 0.05f, 1e-6f);
    CHECK_NEAR(out.pos_z, -0.15f, 1e-6f);
    // The angle goes through the tangent round trip, so it is not a flat multiply - but
    // for a head-sized angle it lands within a fraction of a degree of one.
    CHECK(out.yaw < s.yaw && out.yaw > 0.0f);
    CHECK_NEAR(out.yaw, std::atan(std::tan(10.0f * 3.14159265f / 180.0f) * factor)
                        * 180.0f / 3.14159265f, 1e-4f);
    CHECK(out.pitch < s.pitch && out.pitch > 0.0f);
    // Sign is carried through, so a look to the left stays to the left.
    FrameSample mirrored = s;
    mirrored.yaw = -10.0f;
    CHECK_NEAR(ScaleForZoom(mirrored, factor).yaw, -out.yaw, 1e-5f);
    CHECK(out.has_rotation && out.has_position);
}

// The claim itself, stated as arithmetic rather than as a factor. Displacement across the
// screen goes as tan(angle)/tan(fov/2), so the compensated pose has to hold that quotient
// fixed through a zoom.
void TestZoomHoldsScreenDisplacementFixed() {
    const float base = 82.5f, zoomed = 45.0f;
    const ZoomScale z = DecideZoomScale(zoomed, base, 0.0f);
    CHECK(z.known);

    FrameSample s;
    s.has_rotation = true;
    s.yaw = 9.0f;
    const float scaled = ScaleForZoom(s, z.factor).yaw;

    const float atBase = std::tan(ToRad(s.yaw)) / std::tan(ToRad(base * 0.5f));
    const float atZoom = std::tan(ToRad(scaled)) / std::tan(ToRad(zoomed * 0.5f));
    CHECK_NEAR(atZoom, atBase, 1e-5f);
}

// [View] Fov moves both angles and tan is not linear, so the ratio has to be taken AFTER
// the override rather than before it. Taking it before leaves the compensation wrong by
// exactly the amount the player widened their view, which they would blame on the
// setting.
void TestZoomTakesTheRatioAfterTheFovOverride() {
    const float base = 82.5f, zoomed = 45.0f, requested = 115.5f;  // 1.4x
    const ZoomScale withOverride = DecideZoomScale(zoomed, base, requested);
    CHECK(withOverride.known);
    // The renderer's own two angles, not the game's.
    CHECK_NEAR(withOverride.renderedBaseFov, requested, 1e-3f);
    CHECK_NEAR(withOverride.renderedFov, zoomed * (requested / base), 1e-3f);
    CHECK_NEAR(withOverride.factor,
               std::tan(ToRad(withOverride.renderedFov * 0.5f))
                   / std::tan(ToRad(withOverride.renderedBaseFov * 0.5f)), 1e-5f);

    // And it is a different number from the one the un-overridden angles give, which is
    // why the order matters at all.
    const ZoomScale withoutOverride = DecideZoomScale(zoomed, base, 0.0f);
    CHECK(std::fabs(withOverride.factor - withoutOverride.factor) > 0.01f);

    // An unzoomed frame still reads 1.0 with the override on, so ordinary play is
    // untouched whatever the player set.
    const ZoomScale open = DecideZoomScale(base, base, requested);
    CHECK(open.known);
    CHECK_NEAR(open.factor, 1.0f, 1e-5f);
}

// A garbage angle means the offset or the accessor no longer fits the build. The pose
// then goes in unscaled: a guessed factor would be a silent sensitivity multiplier on
// every frame of the session.
void TestZoomRefusesAnAngleThatIsNotAnAngle() {
    for (const float bad : { 0.0f, -60.0f, 1e-30f, 500.0f,
                             std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity() }) {
        const ZoomScale live = DecideZoomScale(bad, 82.5f, 0.0f);
        CHECK(!live.known);
        CHECK(live.factor == 1.0f);
        const ZoomScale reference = DecideZoomScale(82.5f, bad, 0.0f);
        CHECK(!reference.known);
        CHECK(reference.factor == 1.0f);
    }
}

// The units check, against the two half-field tangents measured out of the running game's
// own FSceneView alongside the angle its accessor reported on the same frame. The
// accessor's degrees are horizontal-at-16:9, so the vertical tangent is tan(fov/2)/(16/9)
// and the horizontal one is that times the display's aspect. Pairing two different axes
// would still produce a plausible-looking factor, and this is what rules it out.
void TestZoomUnitsMatchTheMeasuredProjection() {
    // steam-win32-20220511, measured on a 32:9 display (aspect 3.5556).
    const float fovA = 82.5f,  tanHA = 1.75395f, tanVA = 0.49330f;
    const float fovB = 115.5f, tanHB = 3.16981f, tanVB = 0.89151f;

    CHECK_NEAR(std::tan(ToRad(fovA * 0.5f)) / (16.0f / 9.0f), tanVA, 1e-3f);
    CHECK_NEAR(std::tan(ToRad(fovB * 0.5f)) / (16.0f / 9.0f), tanVB, 1e-3f);
    CHECK_NEAR(tanHA / tanVA, 32.0f / 9.0f, 1e-3f);
    CHECK_NEAR(tanHB / tanVB, 32.0f / 9.0f, 1e-3f);

    // So the factor derived from the two ANGLES matches the one the two rendered
    // projections give, on either axis. That equality is what makes reading the angles
    // legitimate, and why neither the 16:9 reference nor the display's aspect has to be
    // known to the mod at all.
    const ZoomScale z = DecideZoomScale(fovB, fovA, 0.0f);
    CHECK(z.known);
    CHECK_NEAR(z.factor, tanVB / tanVA, 1e-3f);
    CHECK_NEAR(z.factor, tanHB / tanHA, 1e-3f);
}

// ---- ue3 rotator units (ue3_types.h) ---------------------------------------
//
// An FRotator axis is modular - 65536 units to the turn - and every angle the mod
// injects passes through these four conversions. A sign or a wrap error here points the
// camera somewhere plausible rather than somewhere obviously wrong, which is the failure
// that survives a play test.

void TestDegToUnitsIsTheEngineScale() {
    CHECK(DegToUnits(0.0f) == 0);
    CHECK(DegToUnits(90.0f) == 16384);
    CHECK(DegToUnits(-90.0f) == -16384);
    CHECK(DegToUnits(180.0f) == 32768);
}

// The degrees come off the network, so a full turn or more has to land on the rotation it
// denotes rather than on whatever the overflowing cast produced.
void TestDegToUnitsWrapsWholeTurns() {
    CHECK(DegToUnits(360.0f) == 0);
    CHECK(DegToUnits(720.0f) == 0);
    CHECK(DegToUnits(370.0f) == DegToUnits(10.0f));
    CHECK(DegToUnits(-370.0f) == DegToUnits(-10.0f));
    const std::int32_t huge = DegToUnits(1e30f);
    CHECK(huge > -65536 && huge < 65536);
}

// Non-finite is dropped rather than carried into lround, which is undefined for one.
void TestDegToUnitsRejectsNonFinite() {
    CHECK(DegToUnits(std::numeric_limits<float>::quiet_NaN()) == 0);
    CHECK(DegToUnits(std::numeric_limits<float>::infinity()) == 0);
    CHECK(DegToUnits(-std::numeric_limits<float>::infinity()) == 0);
}

void TestRadiansRoundTripThroughUnits() {
    CHECK(RadToUnits(static_cast<float>(kPi) * 0.5f) == 16384);
    CHECK(RadToUnits(0.0f) == 0);
    CHECK_NEAR(UnitsToRad(16384), static_cast<float>(kPi) * 0.5f, 1e-4f);
    CHECK_NEAR(UnitsToRad(-16384), -static_cast<float>(kPi) * 0.5f, 1e-4f);
    CHECK_NEAR(UnitsToRad(RadToUnits(0.7f)), 0.7f, 1e-4f);
}

// Half-open [-32768, 32767], so 180 degrees has ONE encoding. Two encodings is a live bug
// the first time this is reused to compare a yaw or a roll.
void TestWrapSignedFoldsOntoTheHalfTurn() {
    CHECK(WrapSigned(0) == 0);
    CHECK(WrapSigned(32767) == 32767);
    CHECK(WrapSigned(32768) == -32768);
    CHECK(WrapSigned(-32768) == -32768);
    CHECK(WrapSigned(60000) == -5536);
    CHECK(WrapSigned(-70000) == -4464);
    CHECK(WrapSigned(65536) == 0);
    CHECK(WrapSigned(2147483647) == -1);
}

// The game's own pitch arrives anywhere in int32, so the fold happens BEFORE the compare:
// 60000 units means the same angle as -5536 and must not be clamped as if it were steeper
// than vertical.
void TestClampPitchWrapsBeforeItClamps() {
    CHECK(ClampPitch(10000) == 10000);
    CHECK(ClampPitch(20000) == kMaxPitchUnits);
    CHECK(ClampPitch(-20000) == -kMaxPitchUnits);
    CHECK(ClampPitch(60000) == -5536);
    CHECK(ClampPitch(kMaxPitchUnits) == kMaxPitchUnits);
    CHECK(kMaxPitchUnits == 16383);
}

// ---- ue3 rotation matrices (ue3_rotation.h) --------------------------------
//
// The camera-local yaw branch and the reticle's basis both come from these, so the row
// convention and the composition order are what keep the marker glued to the shot on a
// combined pose.

bool MatNear(const Mat3& got, const float want[3][3], float tol) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!(std::fabs(got.m[i][j] - want[i][j]) <= tol)) return false;
        }
    }
    return true;
}

void TestRotatorToMatrixIdentity() {
    const float want[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    CHECK(MatNear(RotatorToMatrix(0.0f, 0.0f, 0.0f), want, 1e-6f));
    const UE3Rotator zero{ 0, 0, 0 };
    CHECK(MatNear(RotatorToMatrix(zero), want, 1e-6f));
}

// UE3 is left-handed with X forward, Y right, Z up, and row 0 is the forward axis.
void TestForwardAxisFollowsYawAndPitch() {
    float f[3];
    ForwardAxis(RotatorToMatrix(UE3Rotator{ 0, 0, 0 }), f);
    CHECK_NEAR(f[0], 1.0f, 1e-5f);
    CHECK_NEAR(f[1], 0.0f, 1e-5f);
    CHECK_NEAR(f[2], 0.0f, 1e-5f);

    // A quarter turn of yaw puts forward on +Y.
    ForwardAxis(RotatorToMatrix(UE3Rotator{ 0, 16384, 0 }), f);
    CHECK_NEAR(f[0], 0.0f, 1e-4f);
    CHECK_NEAR(f[1], 1.0f, 1e-4f);
    CHECK_NEAR(f[2], 0.0f, 1e-4f);

    // Positive pitch is nose up, so forward gains +Z.
    ForwardAxis(RotatorToMatrix(UE3Rotator{ 8192, 0, 0 }), f);
    CHECK_NEAR(f[2], 0.70710678f, 1e-4f);
}

// The rotator overload skips building the other two rows, so it has to agree with the
// matrix path exactly - including on a rotator with roll in it, which is the term it
// leaves out.
void TestForwardAxisFromARotatorMatchesTheMatrix() {
    const UE3Rotator cases[] = {
        { 0, 0, 0 },
        { 4096, 8192, 2048 },
        { -4096, -24576, 16384 },
        { 8000, 32000, -8000 },
    };
    for (const UE3Rotator& r : cases) {
        float viaMatrix[3], direct[3];
        ForwardAxis(RotatorToMatrix(r), viaMatrix);
        ForwardAxis(r, direct);
        CHECK(direct[0] == viaMatrix[0]);
        CHECK(direct[1] == viaMatrix[1]);
        CHECK(direct[2] == viaMatrix[2]);
    }
}

void TestMatrixToRotatorRoundTrips() {
    const UE3Rotator cases[] = {
        { 0, 0, 0 },
        { 4096, 8192, 2048 },
        { -4096, -24576, 1024 },
        { 8000, 32000, -8000 },
    };
    for (const UE3Rotator& r : cases) {
        UE3Rotator back{};
        MatrixToRotator(RotatorToMatrix(r), &back);
        CHECK(std::abs(back.Pitch - r.Pitch) <= 2);
        CHECK(std::abs(WrapSigned(back.Yaw - r.Yaw)) <= 2);
        CHECK(std::abs(WrapSigned(back.Roll - r.Roll)) <= 2);
    }
}

// The camera-local yaw branch composes M_head * M_clean, so a level clean camera leaves
// the head pose exactly as it arrived - which is what makes the two yaw modes coincide
// there.
void TestHeadTimesIdentityIsTheHeadRotator() {
    const UE3Rotator head{ 2048, 6000, -1024 };
    UE3Rotator out{};
    MatrixToRotator(MatMul(RotatorToMatrix(head), RotatorToMatrix(UE3Rotator{ 0, 0, 0 })),
                    &out);
    CHECK(std::abs(out.Pitch - head.Pitch) <= 2);
    CHECK(std::abs(WrapSigned(out.Yaw - head.Yaw)) <= 2);
    CHECK(std::abs(WrapSigned(out.Roll - head.Roll)) <= 2);
}

void TestResolveInBasisSplitsAlongTheRows() {
    const Mat3 identity = RotatorToMatrix(UE3Rotator{ 0, 0, 0 });
    const float v[3] = { 0.3f, -0.4f, 0.5f };
    float fwd = 0.0f, right = 0.0f, up = 0.0f;
    ResolveInBasis(identity, v, &fwd, &right, &up);
    CHECK_NEAR(fwd, 0.3f, 1e-6f);
    CHECK_NEAR(right, -0.4f, 1e-6f);
    CHECK_NEAR(up, 0.5f, 1e-6f);

    // A vector along the basis's own forward resolves to (1, 0, 0) whatever the basis.
    const Mat3 turned = RotatorToMatrix(UE3Rotator{ 3000, 12000, 2000 });
    float f[3];
    ForwardAxis(turned, f);
    ResolveInBasis(turned, f, &fwd, &right, &up);
    CHECK_NEAR(fwd, 1.0f, 1e-4f);
    CHECK_NEAR(right, 0.0f, 1e-4f);
    CHECK_NEAR(up, 0.0f, 1e-4f);
}

// ---- head pose injection (head_pose.h) -------------------------------------
//
// The signs. A mirrored axis here points the camera somewhere plausible and sends the
// next person hunting a fault that is not there, so each one is pinned against the
// engine's own convention: UE3 is left-handed with X forward, Y right, Z up, and an
// FRotator axis is 65536 units to the turn.

FrameSample RotationSample(float yaw, float pitch, float roll) {
    FrameSample s;
    s.has_rotation = true;
    s.yaw = yaw;
    s.pitch = pitch;
    s.roll = roll;
    return s;
}

FrameSample PositionSample(float x, float y, float z) {
    FrameSample s;
    s.has_position = true;
    s.pos_x = x;
    s.pos_y = y;
    s.pos_z = z;
    return s;
}

void TestHeadPoseZeroPoseLeavesTheViewpointAlone() {
    for (const bool worldYaw : { true, false }) {
        UE3Vector loc{ 100.0f, 200.0f, 300.0f };
        UE3Rotator rot{ 1000, 2000, 3000 };
        Lean lean;
        CHECK(ApplyHeadPose(worldYaw, FrameSample{}, &loc, &rot, &lean));
        CHECK(rot.Pitch == 1000 && rot.Yaw == 2000 && rot.Roll == 3000);
        CHECK_NEAR(loc.X, 100.0f, 1e-5f);
        CHECK(lean.IsZero());
    }
}

// A NaN in the rotator or in the camera's world position renders a black frame, so the
// pose is dropped at this boundary rather than written into the engine.
void TestHeadPoseDropsANonFinitePose() {
    FrameSample s = RotationSample(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);
    s.has_position = true;
    s.pos_x = 0.2f;
    UE3Vector loc{ 10.0f, 20.0f, 30.0f };
    UE3Rotator rot{ 100, 200, 300 };
    Lean lean;
    CHECK(!ApplyHeadPose(true, s, &loc, &rot, &lean));
    CHECK(rot.Pitch == 100 && rot.Yaw == 200 && rot.Roll == 300);
    CHECK_NEAR(loc.X, 10.0f, 1e-5f);
    CHECK(lean.IsZero());
}

// Horizon-locked yaw is per-axis addition in engine units, with roll mirrored at the
// boundary and yaw and pitch taken as sent.
void TestHeadPoseWorldYawAddsPerAxisWithRollMirrored() {
    UE3Vector loc{ 0.0f, 0.0f, 0.0f };
    UE3Rotator rot{ 0, 0, 0 };
    Lean lean;
    CHECK(ApplyHeadPose(true, RotationSample(10.0f, 5.0f, 3.0f), &loc, &rot, &lean));
    CHECK(rot.Yaw == DegToUnits(10.0f));
    CHECK(rot.Pitch == DegToUnits(5.0f));
    CHECK(rot.Roll == DegToUnits(-3.0f));
}

// The game rotator fields are plain int32 and nothing bounds what the game put there, so
// each axis is folded onto its half-turn BEFORE the add - the sum of two raw ones is
// signed overflow.
void TestHeadPoseWrapsTheGameRotatorBeforeAdding() {
    UE3Vector loc{ 0.0f, 0.0f, 0.0f };
    UE3Rotator rot{ 0, 2147483000, 0 };
    Lean lean;
    CHECK(ApplyHeadPose(true, RotationSample(10.0f, 0.0f, 0.0f), &loc, &rot, &lean));
    CHECK(rot.Yaw == WrapSigned(2147483000) + DegToUnits(10.0f));
}

// A head pitch stacked on an already-steep game camera would pass straight up and invert
// the world, which the player cannot undo by looking back down. Both branches bound the
// head's contribution against the clean pitch BEFORE composing, so neither can reach
// vertical - see TestCameraLocalYawDoesNotFlipPastVertical for why clamping the composed
// result afterwards is not the same thing.
void TestHeadPoseStopsPitchShortOfVertical() {
    UE3Vector loc{ 0.0f, 0.0f, 0.0f };
    Lean lean;
    UE3Rotator up{ 16000, 0, 0 };
    CHECK(ApplyHeadPose(true, RotationSample(0.0f, 30.0f, 0.0f), &loc, &up, &lean));
    CHECK(up.Pitch == kMaxPitchUnits);

    UE3Rotator down{ -16000, 0, 0 };
    CHECK(ApplyHeadPose(true, RotationSample(0.0f, -30.0f, 0.0f), &loc, &down, &lean));
    CHECK(down.Pitch == -kMaxPitchUnits);

    for (const float headPitch : { 30.0f, -30.0f, 80.0f, -80.0f }) {
        for (const int gamePitch : { 16000, -16000, 0 }) {
            for (const bool worldYaw : { true, false }) {
                UE3Rotator rot{ gamePitch, 0, 0 };
                CHECK(ApplyHeadPose(worldYaw, RotationSample(0.0f, headPitch, 0.0f),
                                    &loc, &rot, &lean));
                CHECK(rot.Pitch <= kMaxPitchUnits && rot.Pitch >= -kMaxPitchUnits);
            }
        }
    }
}

// The two yaw modes coincide when the clean camera is level, which is what makes the
// toggle invisible until the player looks steeply up or down.
void TestHeadPoseYawModesAgreeOnALevelCamera() {
    const FrameSample s = RotationSample(12.0f, 7.0f, 4.0f);
    UE3Vector loc{ 0.0f, 0.0f, 0.0f };
    UE3Rotator world{ 0, 0, 0 };
    UE3Rotator local{ 0, 0, 0 };
    Lean lean;
    CHECK(ApplyHeadPose(true, s, &loc, &world, &lean));
    CHECK(ApplyHeadPose(false, s, &loc, &local, &lean));
    CHECK(std::abs(world.Yaw - local.Yaw) <= 2);
    CHECK(std::abs(world.Pitch - local.Pitch) <= 2);
    CHECK(std::abs(world.Roll - local.Roll) <= 2);
}

// The processor axes are mirrored against the engine on x and z, and the scale is world
// units per metre of head travel.
void TestHeadPosePositionAxesAndScale() {
    UE3Vector loc{ 0.0f, 0.0f, 0.0f };
    UE3Rotator rot{ 0, 0, 0 };
    Lean lean;
    CHECK(ApplyHeadPose(true, PositionSample(0.0f, 0.0f, 0.1f), &loc, &rot, &lean));
    CHECK_NEAR(lean.ruf[2], -10.0f, 1e-4f);
    CHECK_NEAR(loc.X, -10.0f, 1e-4f);
    CHECK_NEAR(loc.Y, 0.0f, 1e-4f);
    CHECK_NEAR(loc.Z, 0.0f, 1e-4f);

    UE3Vector right{ 0.0f, 0.0f, 0.0f };
    rot = UE3Rotator{ 0, 0, 0 };
    CHECK(ApplyHeadPose(true, PositionSample(0.1f, 0.0f, 0.0f), &right, &rot, &lean));
    CHECK_NEAR(lean.ruf[0], -10.0f, 1e-4f);
    CHECK_NEAR(right.Y, -10.0f, 1e-4f);

    UE3Vector up{ 0.0f, 0.0f, 0.0f };
    rot = UE3Rotator{ 0, 0, 0 };
    CHECK(ApplyHeadPose(true, PositionSample(0.0f, 0.1f, 0.0f), &up, &rot, &lean));
    CHECK_NEAR(lean.ruf[1], 10.0f, 1e-4f);
    CHECK_NEAR(up.Z, 10.0f, 1e-4f);
}

// The lean goes in the clean orientation basis with a FLAT forward, so it follows body
// facing and a forward lean puts nothing into world Z, where the vertical limits - which
// were applied in tracker space - could not see it.
void TestHeadPoseLeanFollowsBodyFacingAndStaysLevel() {
    UE3Vector loc{ 0.0f, 0.0f, 0.0f };
    UE3Rotator rot{ 8192, 16384, 0 };  // 45 degrees up, a quarter turn round
    Lean lean;
    CHECK(ApplyHeadPose(true, PositionSample(0.0f, 0.0f, 0.1f), &loc, &rot, &lean));
    CHECK_NEAR(loc.X, 0.0f, 1e-3f);
    CHECK_NEAR(loc.Y, -10.0f, 1e-3f);
    CHECK_NEAR(loc.Z, 0.0f, 1e-3f);
}

// Position and rotation are independent: one channel going quiet must not take the other
// with it.
void TestHeadPoseChannelsAreIndependent() {
    UE3Vector loc{ 0.0f, 0.0f, 0.0f };
    UE3Rotator rot{ 0, 0, 0 };
    Lean lean;
    CHECK(ApplyHeadPose(true, RotationSample(10.0f, 0.0f, 0.0f), &loc, &rot, &lean));
    CHECK(rot.Yaw == DegToUnits(10.0f));
    CHECK(lean.IsZero());
    CHECK_NEAR(loc.X, 0.0f, 1e-5f);

    UE3Rotator still{ 0, 0, 0 };
    CHECK(ApplyHeadPose(true, PositionSample(0.0f, 0.0f, 0.1f), &loc, &still, &lean));
    CHECK(still.Yaw == 0 && still.Pitch == 0 && still.Roll == 0);
    CHECK(!lean.IsZero());
}


// Where a windowed game ends up. Pure arithmetic on a window rect and a monitor's work
// area, which is the half of the centring that can be wrong without anyone noticing on
// the machine it was written on: a taskbar on one edge, a monitor that does not start at
// the origin, and a window with no middle left to move to.

// Measured from inside the game: one 5120x1440 screen with a 48 pixel taskbar along the
// bottom edge, and the 1920x1080 window the game opens hard against its top-left corner.
const WindowRect kWork{ 0, 0, 5120, 1392 };
const WindowRect kGameWindow{ 0, 0, 1920, 1080 };

void TestWindowCentresOnTheWorkArea() {
    const Placement placement = CenterOnWorkArea(kGameWindow, kWork);
    CHECK(placement.decision == PlacementDecision::Move);
    CHECK(placement.x == 1600);
    CHECK(placement.y == 156);
}

void TestWindowKeepsTheOffsetOfAMonitorAwayFromTheOrigin() {
    // A second screen to the right of a 1920 wide primary, with a taskbar down its left
    // edge: both the offset and the inset have to survive into the answer.
    const WindowRect work{ 1980, 0, 3840, 1080 };
    const Placement placement = CenterOnWorkArea(WindowRect{ 0, 0, 1280, 720 }, work);
    CHECK(placement.decision == PlacementDecision::Move);
    CHECK(placement.x == 1980 + (1860 - 1280) / 2);
    CHECK(placement.y == (1080 - 720) / 2);
}

void TestWindowThatFillsTheWorkAreaIsLeftAlone() {
    CHECK(CenterOnWorkArea(WindowRect{ 0, 0, 5120, 1440 }, kWork).decision ==
          PlacementDecision::FillsWorkArea);
    CHECK(CenterOnWorkArea(WindowRect{ 0, 0, 5120, 1000 }, kWork).decision ==
          PlacementDecision::FillsWorkArea);
    // Tall but narrow, so the HEIGHT clause is the one that has to fire. Both cases above
    // trip on width, which left the height test free to be compared against work.Width()
    // without anything noticing - and a portrait window would then be centred off the
    // bottom of the screen.
    CHECK(CenterOnWorkArea(WindowRect{ 0, 0, 1920, 1392 }, kWork).decision ==
          PlacementDecision::FillsWorkArea);
    // One pixel smaller in both directions, and pushed off the middle so the answer is
    // the move rather than the already-centred one.
    CHECK(CenterOnWorkArea(WindowRect{ 500, 0, 500 + 5119, 1391 }, kWork).decision ==
          PlacementDecision::Move);
}

void TestWindowAlreadyCentredIsLeftAlone() {
    CHECK(CenterOnWorkArea(WindowRect{ 1600, 156, 1600 + 1920, 156 + 1080 }, kWork).decision ==
          PlacementDecision::AlreadyCentred);
    // A game that rounds the odd half pixel the other way is centred too.
    CHECK(CenterOnWorkArea(WindowRect{ 1601, 157, 1601 + 1920, 157 + 1080 }, kWork).decision ==
          PlacementDecision::AlreadyCentred);
    CHECK(CenterOnWorkArea(WindowRect{ 1603, 156, 1603 + 1920, 156 + 1080 }, kWork).decision ==
          PlacementDecision::Move);
    // And the other way. Every case above sits on the positive side of the tolerance, so
    // the negative arm of the comparison was carrying no weight at all.
    CHECK(CenterOnWorkArea(WindowRect{ 1598, 154, 1598 + 1920, 154 + 1080 }, kWork).decision ==
          PlacementDecision::AlreadyCentred);
    CHECK(CenterOnWorkArea(WindowRect{ 1597, 156, 1597 + 1920, 156 + 1080 }, kWork).decision ==
          PlacementDecision::Move);
}

}  // namespace


// Past vertical the camera-local composition does not report an out-of-range pitch for
// ClampPitch to catch - MatrixToRotator's atan2 is structurally bounded to +/-90 - it
// reports the SAME pitch with yaw and roll each turned by half a revolution, which is the
// view facing backwards and upside down. Measured before the fix: a clean pitch of 80
// with 20 degrees of head pitch turned yaw 45 into -135 and roll 0 into 180. So the head
// pitch is bounded against the clean pitch on the way in, and the yaw the player was
// facing has to survive every steep camera.
void TestCameraLocalYawDoesNotFlipPastVertical() {
    UE3Vector loc{};
    Lean lean;
    for (const int gamePitchDeg : { 60, 70, 75, 80, 85, 89 }) {
        UE3Rotator rot{ DegToUnits(static_cast<float>(gamePitchDeg)), 8192, 0 };
        CHECK(ApplyHeadPose(false, RotationSample(0.0f, 20.0f, 0.0f), &loc, &rot, &lean));
        // Yaw is untouched by a pure head pitch, and roll must not appear from nowhere.
        CHECK(WrapSigned(rot.Yaw) == 8192);
        CHECK(WrapSigned(rot.Roll) == 0);
        CHECK(rot.Pitch <= kMaxPitchUnits && rot.Pitch >= -kMaxPitchUnits);
    }
    // And the same looking straight down.
    for (const int gamePitchDeg : { -60, -75, -85, -89 }) {
        UE3Rotator rot{ DegToUnits(static_cast<float>(gamePitchDeg)), 8192, 0 };
        CHECK(ApplyHeadPose(false, RotationSample(0.0f, -20.0f, 0.0f), &loc, &rot, &lean));
        CHECK(WrapSigned(rot.Yaw) == 8192);
        CHECK(WrapSigned(rot.Roll) == 0);
    }
}

// The composition ORDER, which every other camera-local test leaves unpinned by using a
// clean rotator of zero - a matrix product against identity is commutative, so reversing
// the operands changed nothing anywhere in the suite. Row-vector convention: the head
// rotation is applied in the camera's frame, so it is M_head * M_clean.
void TestCameraLocalYawComposesHeadInTheCameraFrame() {
    UE3Vector loc{};
    Lean lean;
    UE3Rotator rot{ 8192, 8192, 0 };
    CHECK(ApplyHeadPose(false, RotationSample(25.0f, 15.0f, 0.0f), &loc, &rot, &lean));

    UE3Rotator expect{ 8192, 8192, 0 };
    const Mat3 clean = RotatorToMatrix(expect);
    const Mat3 head = RotatorToMatrix(15.0f * kDegToRad, 25.0f * kDegToRad, 0.0f);
    UE3Rotator wanted{};
    MatrixToRotator(MatMul(head, clean), &wanted);
    wanted.Pitch = ClampPitch(wanted.Pitch);

    CHECK(rot.Pitch == wanted.Pitch);
    CHECK(WrapSigned(rot.Yaw) == WrapSigned(wanted.Yaw));
    CHECK(WrapSigned(rot.Roll) == WrapSigned(wanted.Roll));

    // The reversed order is a different camera, so the assertion above has teeth.
    UE3Rotator reversed{};
    MatrixToRotator(MatMul(clean, head), &reversed);
    CHECK(WrapSigned(reversed.Yaw) != WrapSigned(wanted.Yaw) ||
          reversed.Pitch != wanted.Pitch);
}

// The fold applies to every axis, not just yaw. Roll and pitch were both unpinned:
// removing WrapSigned from either left the whole suite green, and the sum of two raw
// int32 rotator fields is signed overflow.
void TestHeadPoseWrapsEveryAxisBeforeAdding() {
    UE3Vector loc{};
    Lean lean;
    UE3Rotator rot{ 2147483000, 2147483000, 2147483000 };
    CHECK(ApplyHeadPose(true, RotationSample(10.0f, 5.0f, 3.0f), &loc, &rot, &lean));
    CHECK(rot.Yaw == WrapSigned(2147483000) + DegToUnits(10.0f));
    CHECK(rot.Roll == WrapSigned(2147483000) + DegToUnits(-3.0f));
    CHECK(rot.Pitch == ClampPitch(WrapSigned(2147483000) +
                                  DegToUnits(BoundedPitchContribution(2147483000, 5.0f))));
}

// ScaleAngleForZoom is atan(tan(a) * factor), which is the identity only either side of
// vertical: at 95 degrees tan has crossed its asymptote and the round trip returns -85,
// snapping the view to the opposite side. It ran on every tracked frame, including the
// ones where nothing is zooming and the factor is exactly 1.
void TestZoomScalingLeavesAWidePoseAlone() {
    FrameSample s{};
    s.has_rotation = true;
    for (const float yaw : { 95.0f, 120.0f, 170.0f, -140.0f }) {
        s.yaw = yaw;
        s.pitch = 0.0f;
        const FrameSample out = ScaleForZoom(s, 1.0f);
        CHECK_NEAR(out.yaw, yaw, 1e-4f);
        // And under a real zoom the sign must still survive.
        const FrameSample zoomed = ScaleForZoom(s, 0.5f);
        CHECK((zoomed.yaw > 0.0f) == (yaw > 0.0f));
    }
}

// The round trip is only the identity either side of vertical, so the input is clamped
// into its domain rather than the decision to use it. Passing a wide angle through
// unscaled instead put a step in the middle of the pose: at a factor of 0.1, 88.999
// degrees scaled to 80.089 while 89.001 passed through untouched, so two head positions a
// five-hundredth of a degree apart wrote angles nine degrees apart.
void TestZoomScalingIsContinuousAcrossItsDomainBound() {
    for (const float factor : { 0.1f, 0.2f, 0.5f }) {
        const float below = ScaleAngleForZoomBounded(88.999f, factor);
        const float above = ScaleAngleForZoomBounded(89.001f, factor);
        CHECK(std::fabs(above - below) < 0.05f);
        // And monotone past the bound, in both directions.
        CHECK(ScaleAngleForZoomBounded(95.0f, factor) >= above);
        CHECK(ScaleAngleForZoomBounded(-95.0f, factor) <=
              ScaleAngleForZoomBounded(-89.001f, factor));
    }
}

// A factor of exactly 1 is ordinary play, where the log reads x1.0000. Nothing may move.
void TestZoomScalingAtUnityIsExactlyIdentity() {
    FrameSample s{};
    s.has_rotation = true;
    s.has_position = true;
    s.yaw = 12.5f; s.pitch = -7.25f; s.roll = 3.0f;
    s.pos_x = 0.11f; s.pos_y = -0.06f; s.pos_z = 0.21f;
    const FrameSample out = ScaleForZoom(s, 1.0f);
    CHECK_NEAR(out.yaw, s.yaw, 0.0f);
    CHECK_NEAR(out.pitch, s.pitch, 0.0f);
    CHECK_NEAR(out.roll, s.roll, 0.0f);
    CHECK_NEAR(out.pos_x, s.pos_x, 0.0f);
    CHECK_NEAR(out.pos_z, s.pos_z, 0.0f);
}

// The two DecideFov calls gate independently, so a wide shot can scale past the
// renderable ceiling and fall back to the game's own angle while the reference is still
// the overridden one. The ratio between those two is not a zoom, and at a 2x override on
// a 75 degree base it priced a 90 degree frame at 0.268 where the frame actually drawn
// wants 1.303 - tracking five times too weak, with no diagnostic.
void TestZoomScaleRefusesWhenTheTwoAnglesGateDifferently() {
    // Both applied: an ordinary frame, factor 1.0 because nothing is zooming.
    const ZoomScale plain = DecideZoomScale(75.0f, 75.0f, 150.0f);
    CHECK(plain.known);
    CHECK_NEAR(plain.factor, 1.0f, 1e-4f);

    // 90 * (150/75) = 180, which is not renderable, while the base still is. Mismatched
    // decisions must refuse rather than divide one by the other.
    const ZoomScale mismatched = DecideZoomScale(90.0f, 75.0f, 150.0f);
    CHECK(!mismatched.known);
    CHECK_NEAR(mismatched.factor, 1.0f, 1e-4f);
}

// kMinBaseFov is a build-fit test for the camera's UNZOOMED angle - below it the struct
// offset no longer fits the build. Applying it to the live angle switched compensation
// off at exactly the magnifications it exists for: a 7.5x scope on a 75 degree base
// renders at 10 degrees, and the pose went in unscaled at the moment it needed scaling
// most.
void TestZoomScaleStillEngagesThroughANarrowScope() {
    const ZoomScale scoped = DecideZoomScale(9.0f, 75.0f, 0.0f);
    CHECK(scoped.known);
    // A narrower frame magnifies, so the pose must be scaled DOWN.
    CHECK(scoped.factor < 1.0f);
}

// The nav-cluster codes are pinned above; these are the other half of the same binding.
// AGENTS.md fixes the cluster order so the same action lands on the same chord in every
// mod, and a reshuffle here is invisible to every other test.
void TestChordLettersMatchTheFleetOrder() {
    CHECK(kChordToggleKey    == 'Y');
    CHECK(kChordCycleModeKey == 'G');
    CHECK(kChordYawModeKey   == 'H');
    CHECK(kChordAdsModeKey   == 'U');
    // Ctrl+Shift+T was the recenter chord before mods stopped keeping a centre, and it
    // stays free so muscle memory cannot fire something else.
    CHECK(kChordToggleKey    != 'T');
    CHECK(kChordCycleModeKey != 'T');
    CHECK(kChordYawModeKey   != 'T');
    CHECK(kChordAdsModeKey   != 'T');
}

void TestHudProjectionAccountsForReticleDepth() {
    const float parent[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 12800,7200,0,1};
    float world[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 12800,7200,1000,1};
    const float view[16] = {1,0,0,0, 0,-1,0,0, 0,0,-1,0, -12800,7200,-20746.6055f,1};
    const float projection[16] = {1.08055234f,0,0,0, 0,1.920982f,0,0, 0,0,-1,-1, 0,0,-1,0};
    float x = 0, y = 0;
    for (float depth : {0.0f, 1000.0f, 5000.0f}) {
        world[14] = depth;
        CHECK(ProjectHudOffset(world, parent, view, projection, -0.415f, -0.221f, &x, &y));
        const auto clip = TransformHudVector(projection, TransformHudVector(view,
            {world[12] + x * 20, world[13] + y * 20, depth, 1}));
        CHECK_NEAR(clip[0] / clip[3], -0.415f, 0.00001f);
        CHECK_NEAR(clip[1] / clip[3], -0.221f, 0.00001f);
    }
}

void TestHudProjectionHandlesTransformedParents() {
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float parent[16] = {0,2,0.2f,0, -3,0,0.1f,0, 0,0,1,0, 12,5,-300,1};
    const float projection[16] = {1,0,0,0, 0,2,0,0, 0,0,-1,-1, 0,0,-1,0};
    float x = 0, y = 0;
    CHECK(ProjectHudOffset(parent, parent, identity, projection, 0.2f, -0.1f, &x, &y));
    const auto clip = TransformHudVector(projection, TransformHudVector(parent, {x * 20, y * 20, 0, 1}));
    CHECK_NEAR(clip[0] / clip[3], 12.0f / 300.0f + 0.2f, 0.00001f);
    CHECK_NEAR(clip[1] / clip[3], 10.0f / 300.0f - 0.1f, 0.00001f);
    const float collapsed[16] = {};
    CHECK(!ProjectHudOffset(parent, collapsed, identity, projection, 0.2f, 0, &x, &y));
    CHECK(!ProjectHudOffset(identity, identity, identity, projection, 0.2f, 0, &x, &y));
}

int main() {
    TestAdsConfigDefaultsToPaused();
    TestAdsConfigBindsInsertAndTheChord();
    TestChordLettersMatchTheFleetOrder();
    TestAdsConfigUnknownValueFallsBackToTheDefault();
    TestAdsConfigCycleOrder();

    TestAdsPoseHipPassesThrough();
    TestAdsPoseEntryFrameIsIdentity();
    TestAdsPoseRollStaysAbsolute();
    TestAdsPoseYawCrossesTheSeamTheShortWay();
    TestAdsPosePitchAndPositionAreRelative();
    TestAdsPoseCaptureWaitsForALiveRotation();
    TestAdsPoseLoweringTheWeaponDropsTheEntry();

    TestAdsBlendHipIsTheHeadPose();
    TestAdsBlendPausedKeepsRollAndDropsTheRest();
    TestAdsBlendTrackedLandsOnTheEntryRelativePose();

    TestAdsFadeReversalStartsFromWhereItIs();
    TestAdsFadeSuppressionResetsToTheHip();

    TestGatePausedSuspendsAndStillReportsTheSights();
    TestGateTrackedModesKeepTheGateOpen();
    TestGateHipFireIsActiveInEveryMode();
    TestGateSuppressionOutranksAdsAndClearsTheFlag();
    TestGateNoTrackerOutranksAds();
    TestGateAdsStateHealsWithoutAnExitEdge();

    TestAimMarkerOffSuppressesEveryMode();
    TestAimMarkerOnDrawsAtTheHipInEveryMode();
    TestAimMarkerWithSightsUpIsMarkerModeOnly();
    TestAimMarkerHoldsInPausedUntilTheFadeHasLanded();

    TestFovOffLeavesTheGameAngleAlone();
    TestFovUnzoomedFrameRendersTheConfiguredAngle();
    TestFovZoomKeepsItsFactor();
    TestFovRefusesAnImpossibleBase();
    TestFovRefusesAFrameWithNoProjection();

    TestZoomIsExactlyOneWhenNothingZooms();
    TestZoomIsTheTangentRatioNotTheAngleRatio();
    TestZoomScalesYawPitchAndPositionButNotRoll();
    TestZoomHoldsScreenDisplacementFixed();
    TestZoomTakesTheRatioAfterTheFovOverride();
    TestZoomRefusesAnAngleThatIsNotAnAngle();
    TestZoomUnitsMatchTheMeasuredProjection();

    TestDegToUnitsIsTheEngineScale();
    TestDegToUnitsWrapsWholeTurns();
    TestDegToUnitsRejectsNonFinite();
    TestRadiansRoundTripThroughUnits();
    TestWrapSignedFoldsOntoTheHalfTurn();
    TestClampPitchWrapsBeforeItClamps();

    TestRotatorToMatrixIdentity();
    TestForwardAxisFollowsYawAndPitch();
    TestForwardAxisFromARotatorMatchesTheMatrix();
    TestMatrixToRotatorRoundTrips();
    TestHeadTimesIdentityIsTheHeadRotator();
    TestResolveInBasisSplitsAlongTheRows();

    TestHeadPoseZeroPoseLeavesTheViewpointAlone();
    TestHeadPoseDropsANonFinitePose();
    TestHeadPoseWorldYawAddsPerAxisWithRollMirrored();
    TestHeadPoseWrapsTheGameRotatorBeforeAdding();
    TestCameraLocalYawDoesNotFlipPastVertical();
    TestCameraLocalYawComposesHeadInTheCameraFrame();
    TestHeadPoseWrapsEveryAxisBeforeAdding();
    TestZoomScalingLeavesAWidePoseAlone();
    TestZoomScalingAtUnityIsExactlyIdentity();
    TestZoomScalingIsContinuousAcrossItsDomainBound();
    TestZoomScaleRefusesWhenTheTwoAnglesGateDifferently();
    TestZoomScaleStillEngagesThroughANarrowScope();
    TestHeadPoseStopsPitchShortOfVertical();
    TestHeadPoseYawModesAgreeOnALevelCamera();
    TestHeadPosePositionAxesAndScale();
    TestHeadPoseLeanFollowsBodyFacingAndStaysLevel();
    TestHeadPoseChannelsAreIndependent();

    TestHudProjectionAccountsForReticleDepth();
    TestHudProjectionHandlesTransformedParents();

    TestWindowCentresOnTheWorkArea();
    TestWindowKeepsTheOffsetOfAMonitorAwayFromTheOrigin();
    TestWindowThatFillsTheWorkAreaIsLeftAlone();
    TestWindowAlreadyCentredIsLeftAlone();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
