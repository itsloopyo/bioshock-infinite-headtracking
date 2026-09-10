#pragma once

#include "frame_sample.h"
#include "ue3_rotation.h"
#include "ue3_types.h"

#include <cmath>

namespace BioShockInfiniteHeadTracking {

// The positional lean applied to the render eye this frame: along the clean camera's
// right, up and forward axes, and as the world vector those add up to. Both are needed -
// the world vector is what is added to the viewpoint, and the components are what the
// diagnostic line reports, because a lean reads against the axis it was asked for on.
struct Lean {
    float ruf[3] = { 0.0f, 0.0f, 0.0f };
    float world[3] = { 0.0f, 0.0f, 0.0f };

    bool IsZero() const {
        return world[0] == 0.0f && world[1] == 0.0f && world[2] == 0.0f;
    }
};


// The pose comes off the network and everything downstream writes it into the engine's
// own out-parameters, so this is the boundary worth validating at: a NaN in the rotator
// or in the camera's world position renders a black frame, and the camera-local branch
// would carry it into lround, which is undefined for one.
//
// Exposed rather than kept inside ApplyHeadPose because the ADS entry pose latches a
// sample BEFORE this point, and a non-finite one banked there poisons every later frame
// of that aim.
inline bool PoseIsFinite(const FrameSample& s) {
    return std::isfinite(s.yaw) && std::isfinite(s.pitch) && std::isfinite(s.roll) &&
           std::isfinite(s.pos_x) && std::isfinite(s.pos_y) && std::isfinite(s.pos_z);
}

// Adds one frame's head pose to the viewpoint the engine just filled in, and reports the
// positional part of it back through outLean.
//
// Pure arithmetic on the two out-parameters, with nothing logged and nothing global read,
// so the axis signs, the unit conversion and the pitch clamp can be exercised without the
// game - the same reason the ADS gate is a free function (ads_gate.h). Returns false with
// the viewpoint untouched and the lean zero when the pose is not a finite number; saying
// so is the caller's job.
inline bool ApplyHeadPose(bool worldSpaceYaw, const FrameSample& s, UE3Vector* outLoc,
                          UE3Rotator* outRot, Lean* outLean) {
    // Written through the out-parameter rather than copied out at each return: with three
    // exits, the copy is a line one of them eventually forgets, and a missing lean reaches
    // the reticle as the previous frame's parallax correction.
    Lean& lean = *outLean;
    lean = Lean{};

    if (!PoseIsFinite(s)) {
        return false;
    }

    // The tracker declares no convention of its own, so the mirrored axes are flipped
    // here, once, where the tracker meets the engine. Yaw and pitch match UE3 directly
    // and roll is mirrored - the same three signs the other two UE3 mods in this fleet
    // arrived at and verified in game (spec-ops-the-line, dishonored).
    const float headYaw   =  s.yaw;
    const float headPitch =  s.pitch;
    const float headRoll  = -s.roll;

    // 6DOF position goes in the clean orientation basis, before head rotation is added,
    // so a lean follows body facing rather than the head-rotated view. UE3 is
    // left-handed with X forward, Y right, Z up.
    if (s.has_position) {
        // Horizon-locked: forward is FLAT, so the three axes are orthogonal and a lean
        // moves the eye by the amount asked for along the axis asked for. Using the
        // pitched forward instead puts part of a forward lean into world Z, where the
        // vertical limits - already applied, in tracker space - cannot see it.
        const float yawRad = UnitsToRad(outRot->Yaw);
        const float cy = std::cos(yawRad), sy = std::sin(yawRad);

        const float fwd[3]   = { cy,   sy,   0.0f };
        const float right[3] = { -sy,  cy,   0.0f };
        const float up[3]    = { 0.0f, 0.0f, 1.0f };

        // The processor's own axes are mirrored against the engine's on x and z: its
        // negative z is the forward lean, which is what puts the generous LimitZ on
        // leaning in and the restricted LimitZBack on pulling away. Converted here,
        // after the clamp rather than before it, because doing it with the processor's
        // inversion instead hands the 0.40 m budget to the backward lean.
        const float oR = -s.pos_x * kWorldUnitsPerMetre;
        const float oU =  s.pos_y * kWorldUnitsPerMetre;
        const float oF = -s.pos_z * kWorldUnitsPerMetre;
        lean.ruf[0] = oR;
        lean.ruf[1] = oU;
        lean.ruf[2] = oF;

        // Kept in world units as well as in the clean basis: the reticle projection
        // needs the vector the render eye actually moved by, not its components.
        lean.world[0] = right[0] * oR + up[0] * oU + fwd[0] * oF;
        lean.world[1] = right[1] * oR + up[1] * oU + fwd[1] * oF;
        lean.world[2] = right[2] * oR + up[2] * oU + fwd[2] * oF;
        outLoc->X += lean.world[0];
        outLoc->Y += lean.world[1];
        outLoc->Z += lean.world[2];
    }

    if (!s.has_rotation) {
        return true;
    }

    // Bounded against the clean camera BEFORE it is composed, in both branches.
    //
    // Clamping the composed pitch afterwards is not the same thing, and the camera-local
    // branch is where the difference bites: past vertical that composition does not
    // produce an out-of-range pitch to clamp, it produces the same pitch with yaw and
    // roll each turned by half a revolution - the view backwards and upside down, which
    // ClampPitch cannot see. Measured on this code before the fix: a clean pitch of 80
    // degrees with 20 degrees of head pitch came out as yaw -135 (from 45) and roll 180.
    const float boundedPitch = BoundedPitchContribution(outRot->Pitch, headPitch);

    if (worldSpaceYaw) {
        // Horizon-locked yaw (default): FRotator yaw is the outermost rotation about
        // world Z, so per-axis addition keeps head yaw on the world up-axis no matter
        // how the camera is pitched. Pitch and roll stay camera-relative.
        //
        // Each axis is folded onto its half-turn BEFORE the add. The engine's rotator
        // fields are plain int32 and nothing bounds what the game put there, so adding
        // up to a revolution to a raw one is signed overflow, which is undefined. After
        // the fold each operand is inside +/-65536, so the worst-case sum is well within
        // int32.
        outRot->Yaw   = WrapSigned(outRot->Yaw) + DegToUnits(headYaw);
        outRot->Roll  = WrapSigned(outRot->Roll) + DegToUnits(headRoll);
        // Pitch is stopped one unit short of vertical: a head pitch stacked on an
        // already-steep game camera would otherwise pass straight up and invert the
        // world, which the player cannot undo by looking back down.
        outRot->Pitch = ClampPitch(WrapSigned(outRot->Pitch) + DegToUnits(boundedPitch));
    } else {
        // Camera-local yaw: compose the head rotation in the camera frame
        // (M_head * M_clean, row-vector convention) so yaw follows the tilted up-axis at
        // extreme pitches. Coincides with the horizon-locked branch when the clean
        // camera is level.
        const Mat3 clean = RotatorToMatrix(*outRot);
        const Mat3 head = RotatorToMatrix(boundedPitch * kDegToRad, headYaw * kDegToRad,
                                          headRoll * kDegToRad);
        MatrixToRotator(MatMul(head, clean), outRot);
        outRot->Pitch = ClampPitch(outRot->Pitch);
    }
    return true;
}

}  // namespace BioShockInfiniteHeadTracking
