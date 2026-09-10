#pragma once

#include <cstddef>

namespace BioShockInfiniteHeadTracking {

// Dumps a window of an engine object to the log at a fixed interval, as rows of hex
// dwords, 8 to a row.
//
// This is the tool for finding the field that separates gameplay from a menu or a pause
// screen on a build whose layout has moved. It is opt-in ([Diagnostics] StateProbe),
// because it writes several kilobytes a second: turn it on, walk the game through the
// states, and diff the dumps.
void EnableStateProbe(bool on);

// Whether it is on. Read by the parts of the mod that have something worth dumping only
// when a maintainer is looking - the reflective property dumps, which run once and cost
// several hundred log lines.
bool StateProbeEnabled();

// Called once per rendered frame with the player controller the scene view is being
// built for. Rate-limits itself; does nothing at all when the probe is off.
void StateProbeTick(const void* playerController);

}  // namespace BioShockInfiniteHeadTracking
