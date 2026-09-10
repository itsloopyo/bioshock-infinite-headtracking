#pragma once

namespace BioShockInfiniteHeadTracking {

// Centres the game's window on the work area of the monitor it is already on, each time
// the game gives it a new size - the window coming up at startup, and the resolution
// changing in the options menu. A fullscreen or borderless window has no middle to move
// to and is left alone, as is one the game centred itself.
//
// Owns a thread that is never joined; see the note in dllmain.cpp about what running
// anything on the exit path would deadlock against.
void StartWindowCentering();

}  // namespace BioShockInfiniteHeadTracking
