#pragma once

namespace BioShockInfiniteHeadTracking {

// One frame's processed head pose: rotation in degrees (YPR) and position offset
// in metres (tracker basis: x=right, y=up, z=forward). has_* report whether each
// channel produced fresh data this frame.
struct FrameSample {
    bool  has_rotation = false;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    bool  has_position = false;
    float pos_x = 0.0f, pos_y = 0.0f, pos_z = 0.0f;
};

}  // namespace BioShockInfiniteHeadTracking
