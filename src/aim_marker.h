#pragma once

#include <atomic>

namespace BioShockInfiniteHeadTracking {

// The clean aim direction projected through the tracked view. Positional parallax
// still needs a measured hit distance; this state only corrects head rotation.
struct AimMarker {
    // The clean aim direction expressed in the tracked view's basis: right, up, forward.
    // Normalised, so `forward` is a cosine.
    std::atomic<float> right{0.0f};
    std::atomic<float> up{0.0f};
    std::atomic<float> forward{1.0f};

    // Half-field tangents of the projection the frame was built with, taken from
    // 1/M[0][0] and 1/M[1][1] of the FSceneView's own projection matrix. Reading them
    // off the matrix rather than deriving them from a field-of-view angle and an assumed
    // aspect convention is what removes every guess from the placement, and what makes
    // an ultrawide display just work.
    std::atomic<float> tan_half_h{0.0f};
    std::atomic<float> tan_half_v{0.0f};
    // The stock reticle keeps its native position until the projection is available.
    std::atomic<bool> projection_valid{false};

    std::atomic<bool> active{false};
};

AimMarker& GetAimMarker();

}  // namespace BioShockInfiniteHeadTracking
