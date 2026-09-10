#pragma once

#include "aim_marker.h"

#include <cmath>

namespace BioShockInfiniteHeadTracking {

enum class AimProjection {
    Ok,
    Inactive,
    NoProjection,
    Behind,
    NotFinite,
};

// The aim has swung more than 84 degrees off the rendered view, which is outside any
// frame this game draws. The guard is on the forward component APPROACHING zero rather
// than merely going negative: the projection diverges either side of it, and a marker at
// 1e30 is a NaN on its way to a vertex buffer.
//
// The vector published by the camera hook is normalised, so this is an angle.
constexpr float kMinForward = 0.1f;

// Where the shot lands in the head-tracked view, in normalised device coordinates:
// x right, y up, -1 to 1 across the drawn image.
//
// ONE derivation, used by every consumer. Two copies of this arithmetic is how a reticle
// and a marker beside it end up agreeing on single-axis poses and disagreeing on
// combined ones.
//
// The camera hook resolves the clean aim into the tracked view's own basis, so no
// rotation composition is re-derived here: the numbers arriving are already components
// along the axes the frame was drawn with, and the tangents are the ones the projection
// matrix divided by. That is also why nothing here knows or cares what shape the display
// is.
inline AimProjection ProjectAim(float* ndcX, float* ndcY) {
    const AimMarker& marker = GetAimMarker();
    // Acquire, paired with the release in PublishAimMarker: the components below are
    // only guaranteed to be this frame's once `active` has been observed set.
    if (!marker.active.load(std::memory_order_acquire)) {
        return AimProjection::Inactive;
    }
    if (!marker.projection_valid.load(std::memory_order_acquire)) {
        return AimProjection::NoProjection;
    }

    // The components are the clean aim resolved in an orthonormal basis, so the vector is
    // already unit length and `forward` is already the cosine - no normalise step, and no
    // divide-by-length guard for a length that is 1.0 by construction. Behind the camera
    // is a real case and is what this tests.
    const float fwd = marker.forward.load(std::memory_order_relaxed);
    const float right = marker.right.load(std::memory_order_relaxed);
    const float up = marker.up.load(std::memory_order_relaxed);
    if (!(fwd > kMinForward)) {
        return AimProjection::Behind;
    }

    // Both tangents are positive whenever projection_valid is set - PublishProjection
    // will not set it otherwise - so they are read, not re-tested.
    const float tanH = marker.tan_half_h.load(std::memory_order_relaxed);
    const float tanV = marker.tan_half_v.load(std::memory_order_relaxed);

    *ndcX = (right / fwd) / tanH;
    *ndcY = (up / fwd) / tanV;
    if (!std::isfinite(*ndcX) || !std::isfinite(*ndcY)) {
        return AimProjection::NotFinite;
    }
    return AimProjection::Ok;
}

inline const char* Describe(AimProjection r) {
    switch (r) {
        case AimProjection::Ok:           return "ok";
        case AimProjection::Inactive:     return "no mark this frame";
        case AimProjection::NoProjection: return "the frame's projection matrix has not been found";
        case AimProjection::Behind:       return "the aim is more than 84 degrees off the view";
        case AimProjection::NotFinite:    return "the projected position is not a finite number";
    }
    return "unknown";
}

}  // namespace BioShockInfiniteHeadTracking
