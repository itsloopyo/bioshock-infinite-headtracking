#pragma once

#include <cmath>

namespace BioShockInfiniteHeadTracking {

// Boundary validation for floats read from the user-editable INI. The core
// library already finite-checks rotation values arriving over UDP
// (OpenTrackPacket::FiniteFloat); the same guarantee must hold for config
// values, which feed into the identical smoothing/quaternion math. A NaN/Inf
// from a malformed INI (e.g. "LocalSmoothing=nan") otherwise poisons the
// smoothed quaternion and the injected view matrix.

inline float SanitizeFinite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}

inline float ClampRange(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Smoothing must be finite and within [0,1], which is the scale both keys are
// documented and written on - anything outside it is a typo rather than a setting, and
// the INI is a boundary this side does not get to trust. Not because of anything
// downstream: core's CalculateSmoothingFactor clamps its own speed in both overloads, so
// an out-of-range value there would be absorbed rather than inverted.
//
// This is validation, not a floor: any value the user sets inside [0,1] reaches the
// processor untouched. Applies to both LocalSmoothing and RemoteSmoothing; `fallback` is
// that key's shipped default.
inline float SanitizeSmoothing(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, 1.0f);
}

// Position limit in metres: finite, and non-negative because the sign is not a
// tuning choice. PositionProcessor::ClampToLimits calls Clamp(v, -limit, +limit); a
// negative limit hands it lo > hi, which pins the offset at a constant instead of
// bounding it. Magnitude is left alone - a wider range than the shipped default is a
// legitimate choice, and the README quotes no ceiling.
inline float SanitizePositionLimit(float v, float fallback) {
    const float f = SanitizeFinite(v, fallback);
    return f < 0.0f ? 0.0f : f;
}

}  // namespace BioShockInfiniteHeadTracking
