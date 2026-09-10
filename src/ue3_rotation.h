#pragma once

#include "ue3_types.h"

#include <cmath>

namespace BioShockInfiniteHeadTracking {

// UE3 FRotationMatrix layout: rows are the world-space forward/right/up axes
// (row-vector convention), composed as M(P,Y,R) = M(P,0,R) * M(0,Y,0). Yaw is
// the outermost rotation about world Z - which is why plain FRotator addition
// gives horizon-locked yaw, and why camera-local yaw needs this matrix path.
struct Mat3 {
    float m[3][3];
};

inline Mat3 RotatorToMatrix(float pitchRad, float yawRad, float rollRad) {
    const float sp = std::sin(pitchRad), cp = std::cos(pitchRad);
    const float sy = std::sin(yawRad),   cy = std::cos(yawRad);
    const float sr = std::sin(rollRad),  cr = std::cos(rollRad);
    Mat3 M;
    M.m[0][0] = cp * cy;
    M.m[0][1] = cp * sy;
    M.m[0][2] = sp;
    M.m[1][0] = sr * sp * cy - cr * sy;
    M.m[1][1] = sr * sp * sy + cr * cy;
    M.m[1][2] = -sr * cp;
    M.m[2][0] = -(cr * sp * cy + sr * sy);
    M.m[2][1] = sr * cy - cr * sp * sy;
    M.m[2][2] = cr * cp;
    return M;
}

inline Mat3 RotatorToMatrix(const UE3Rotator& r) {
    return RotatorToMatrix(UnitsToRad(r.Pitch), UnitsToRad(r.Yaw), UnitsToRad(r.Roll));
}

inline Mat3 MatMul(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

// UE3 FMatrix::Rotator(): pitch and yaw from the forward axis, roll from the
// right and up axes projected onto the roll-free right axis.
inline void MatrixToRotator(const Mat3& M, UE3Rotator* out) {
    const float fx = M.m[0][0], fy = M.m[0][1], fz = M.m[0][2];
    const float pitch = std::atan2(fz, std::sqrt(fx * fx + fy * fy));
    const float yaw   = std::atan2(fy, fx);
    const float syx = -std::sin(yaw), syy = std::cos(yaw);
    const float roll  = std::atan2(M.m[2][0] * syx + M.m[2][1] * syy,
                                   M.m[1][0] * syx + M.m[1][1] * syy);
    out->Pitch = RadToUnits(pitch);
    out->Yaw   = RadToUnits(yaw);
    out->Roll  = RadToUnits(roll);
}

// Row 0 of a rotation built by RotatorToMatrix is the world-space forward axis.
inline void ForwardAxis(const Mat3& M, float out[3]) {
    out[0] = M.m[0][0];
    out[1] = M.m[0][1];
    out[2] = M.m[0][2];
}

// The same axis without building the other two rows. Roll does not appear in row 0, so
// this is the identical expression with the roll sine and cosine and the six products of
// the other rows left uncomputed - which is what the camera hook wants from the clean
// rotator every rendered frame.
inline void ForwardAxis(const UE3Rotator& r, float out[3]) {
    const float sp = std::sin(UnitsToRad(r.Pitch)), cp = std::cos(UnitsToRad(r.Pitch));
    const float sy = std::sin(UnitsToRad(r.Yaw)),   cy = std::cos(UnitsToRad(r.Yaw));
    out[0] = cp * cy;
    out[1] = cp * sy;
    out[2] = sp;
}

// Resolves a world-space vector into the basis whose rows are M's axes, giving its
// components along that basis's forward, right and up.
inline void ResolveInBasis(const Mat3& M, const float v[3], float* outForward,
                           float* outRight, float* outUp) {
    *outForward = v[0] * M.m[0][0] + v[1] * M.m[0][1] + v[2] * M.m[0][2];
    *outRight   = v[0] * M.m[1][0] + v[1] * M.m[1][1] + v[2] * M.m[1][2];
    *outUp      = v[0] * M.m[2][0] + v[1] * M.m[2][1] + v[2] * M.m[2][2];
}

}  // namespace BioShockInfiniteHeadTracking
