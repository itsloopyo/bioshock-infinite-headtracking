#pragma once

#include <array>
#include <cmath>

namespace BioShockInfiniteHeadTracking {

using HudVector = std::array<float, 4>;

inline HudVector TransformHudVector(const float* matrix, const HudVector& v) {
    HudVector result{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            result[row] += matrix[col * 4 + row] * v[col];
        }
    }
    return result;
}

inline bool ProjectHudOffset(const float* world, const float* parent,
                              const float* view, const float* projection,
                              float ndcX, float ndcY, float* x, float* y) {
    const auto project = [view, projection](const HudVector& v) {
        return TransformHudVector(projection, TransformHudVector(view, v));
    };
    const HudVector origin = project({world[12], world[13], world[14], world[15]});
    // Scaleform matrices use twips; SetPosition takes pixels.
    const HudVector horizontal = project({parent[0] * 20.0f, parent[1] * 20.0f,
                                           parent[2] * 20.0f, parent[3] * 20.0f});
    const HudVector vertical = project({parent[4] * 20.0f, parent[5] * 20.0f,
                                         parent[6] * 20.0f, parent[7] * 20.0f});
    if (!(origin[3] > 0.0f)) {
        return false;
    }
    const float targetX = origin[0] / origin[3] + ndcX;
    const float targetY = origin[1] / origin[3] + ndcY;
    const float a = horizontal[0] - targetX * horizontal[3];
    const float b = vertical[0] - targetX * vertical[3];
    const float c = horizontal[1] - targetY * horizontal[3];
    const float d = vertical[1] - targetY * vertical[3];
    const float determinant = a * d - b * c;
    if (determinant == 0.0f) {
        return false;
    }
    *x = origin[3] * (ndcX * d - ndcY * b) / determinant;
    *y = origin[3] * (ndcY * a - ndcX * c) / determinant;
    return std::isfinite(*x) && std::isfinite(*y);
}

}  // namespace BioShockInfiniteHeadTracking
