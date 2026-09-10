#pragma once

#include <cmath>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {

// UE3 POD camera types (32-bit engine: FVector is 3 floats, FRotator is 3 int32
// where 65536 units = 360 degrees). Do NOT use the core ue_math.h FVector or
// FRotator here - those are UE5 doubles and would mis-decode these fields.
struct UE3Vector {
    float X, Y, Z;
};
struct UE3Rotator {
    std::int32_t Pitch, Yaw, Roll;
};

constexpr double kPi = 3.14159265358979323846;

// One full turn of an FRotator axis, as the integer the modular arithmetic below uses.
constexpr std::int32_t kUnitsPerRevolutionInt = 65536;
// The same number for the degree and radian conversions, derived rather than written out
// again so the two cannot drift apart.
constexpr float kUnitsPerRevolution = static_cast<float>(kUnitsPerRevolutionInt);

// The half-open range one revolution folds onto: [-32768, 32767], so 180 degrees has ONE
// encoding. Leaving +32768 in place gives it two, which is harmless for a pitch that is
// about to be clamped to +/-16383 and a live bug the first time the fold is reused to
// compare a yaw or a roll.
constexpr std::int32_t kMaxSignedUnits = kUnitsPerRevolutionInt / 2 - 1;
constexpr std::int32_t kMinSignedUnits = -kUnitsPerRevolutionInt / 2;

// UE3 world units are centimetres, so one metre of head travel is 100 of them. This is
// a fact about the engine, not a preference: it is unit conversion, which is the mod's
// job, and it is deliberately NOT a config key. A user-settable multiplier here would be
// a position sensitivity - and a negative one an axis inversion - which the tracker owns.
constexpr float kWorldUnitsPerMetre = 100.0f;

constexpr float kUnitsToRad = static_cast<float>(2.0 * kPi / kUnitsPerRevolution);
constexpr float kDegToRad   = static_cast<float>(kPi / 180.0);

// An FRotator axis is modular - 65536 units to the turn - so wrapping into one
// revolution is what the engine's own arithmetic does anyway: every angle that already
// fitted converts to the same units, and one that did not lands on the rotation it
// denotes instead of on whatever the overflowing cast produced. Without the wrap the
// cast is undefined for any value int32 cannot hold, and the degrees reaching here come
// off the network rather than from anything this side bounds.
inline std::int32_t DegToUnits(float deg) {
    const double units = static_cast<double>(deg) * (kUnitsPerRevolution / 360.0);
    if (!std::isfinite(units)) {
        return 0;
    }
    return static_cast<std::int32_t>(
        std::lround(std::fmod(units, static_cast<double>(kUnitsPerRevolution))));
}

// No finite guard, unlike DegToUnits: this is only ever reached from MatrixToRotator,
// whose atan2 bounds its input to [-pi, pi], and the camera hook rejects a non-finite
// pose before either yaw branch runs. DegToUnits needs one because the degrees handed
// to it come straight from the pipeline.
inline std::int32_t RadToUnits(float rad) {
    return static_cast<std::int32_t>(std::lround(rad * (kUnitsPerRevolution * 0.5) / kPi));
}

inline float UnitsToRad(std::int32_t units) {
    return static_cast<float>(units) * kUnitsToRad;
}

inline float UnitsToDeg(std::int32_t units) {
    return static_cast<float>(units) * (360.0f / static_cast<float>(kUnitsPerRevolution));
}

// Folds an FRotator axis onto the half-turn either side of zero. The axes are modular,
// so the game's own pitch arrives anywhere in int32 and 60000 units means the same
// angle as -5536: comparing the first against a limit and the second against the same
// limit gives two different answers about the same camera.
inline std::int32_t WrapSigned(std::int32_t units) {
    std::int32_t wrapped = units % kUnitsPerRevolutionInt;
    if (wrapped > kMaxSignedUnits) {
        wrapped -= kUnitsPerRevolutionInt;
    } else if (wrapped < kMinSignedUnits) {
        wrapped += kUnitsPerRevolutionInt;
    }
    return wrapped;
}

// One unit short of straight up or straight down. This is the singularity, not a taste
// limit: past vertical the view is upside down and the player has no way back except to
// pitch their head further, so a composed pitch is stopped here rather than allowed
// through. Normal play never reaches it - the game's own camera is bounded well inside.
constexpr std::int32_t kMaxPitchUnits = 16383;

inline std::int32_t ClampPitch(std::int32_t units) {
    const std::int32_t wrapped = WrapSigned(units);
    if (wrapped > kMaxPitchUnits) {
        return kMaxPitchUnits;
    }
    if (wrapped < -kMaxPitchUnits) {
        return -kMaxPitchUnits;
    }
    return wrapped;
}

// How much of the head's pitch the clean camera has room for before the composed view
// passes vertical.
//
// Clamping the composed RESULT is not enough, and the camera-local branch is where that
// shows. Past vertical the composition does not report an out-of-range pitch for
// ClampPitch to catch - MatrixToRotator's atan2 is structurally bounded to +/-90 - it
// reports the same pitch with yaw and roll each turned by half a revolution, which is the
// view facing backwards and upside down. So the contribution is bounded on the way in.
inline float BoundedPitchContribution(std::int32_t cleanPitchUnits, float headPitchDeg) {
    const float clean = UnitsToDeg(WrapSigned(cleanPitchUnits));
    const float limit = UnitsToDeg(kMaxPitchUnits);
    // Floored at zero either side. A game camera whose own pitch is already past the
    // limit - a scripted shot, or anything that writes a rotator this side does not bound
    // - would otherwise give a NEGATIVE headroom, and the head's zero would be pulled to
    // it: the view rotating while the tracker sits dead centre.
    const float headroomUp = limit - clean > 0.0f ? limit - clean : 0.0f;
    const float headroomDown = -limit - clean < 0.0f ? -limit - clean : 0.0f;
    if (headPitchDeg > headroomUp) {
        return headroomUp;
    }
    if (headPitchDeg < headroomDown) {
        return headroomDown;
    }
    return headPitchDeg;
}

}  // namespace BioShockInfiniteHeadTracking
