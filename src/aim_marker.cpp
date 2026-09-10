#include "aim_marker.h"

namespace BioShockInfiniteHeadTracking {

namespace {
// Namespace scope rather than a function-local static: every member is
// constant-initialised, so there is no one-time-init guard for the callers to test.
// This is read several times a frame from the scene-view hook and the present hook,
// which are the two hottest paths the mod has.
AimMarker g_marker;
}  // namespace

AimMarker& GetAimMarker() {
    return g_marker;
}

}  // namespace BioShockInfiniteHeadTracking
