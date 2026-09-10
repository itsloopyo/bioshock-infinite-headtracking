#pragma once

namespace BioShockInfiniteHeadTracking {

// A window and a monitor as the placement arithmetic sees them, so the decision below is
// a pure function and can be checked without a window to move.
struct WindowRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};

enum class PlacementDecision {
    Move,
    // The window is at least as large as the work area in one direction, which is what
    // fullscreen and borderless look like from here: there is no middle left to move
    // them to, and centring one would push part of the frame off the screen.
    FillsWorkArea,
    AlreadyCentred,
};

struct Placement {
    PlacementDecision decision = PlacementDecision::FillsWorkArea;
    int x = 0;
    int y = 0;
};

// The work area, not the monitor bounds. A taskbar docked to the top or the left moves
// where the middle of the usable screen is, and a window centred against the full monitor
// puts its title bar under that taskbar, where it cannot be dragged back out.
inline Placement CenterOnWorkArea(const WindowRect& window, const WindowRect& work) {
    if (window.Width() >= work.Width() || window.Height() >= work.Height()) {
        return Placement{};
    }

    Placement placement;
    placement.decision = PlacementDecision::Move;
    placement.x = work.left + (work.Width() - window.Width()) / 2;
    placement.y = work.top + (work.Height() - window.Height()) / 2;

    // A game that centres its own window rounds the odd half pixel up where the integer
    // maths here rounds it down, so an exact comparison would move the window by one
    // pixel and report that as a fix.
    constexpr int kTolerance = 2;
    const int dx = window.left - placement.x;
    const int dy = window.top - placement.y;
    if (dx >= -kTolerance && dx <= kTolerance && dy >= -kTolerance && dy <= kTolerance) {
        placement.decision = PlacementDecision::AlreadyCentred;
    }
    return placement;
}

}  // namespace BioShockInfiniteHeadTracking
