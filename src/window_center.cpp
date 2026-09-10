#include "window_center.h"

#include "logging.h"
#include "window_placement.h"

#include "cameraunlock/os/game_window.h"

#include <windows.h>

namespace BioShockInfiniteHeadTracking {

namespace {

// One EnumWindows and one GetWindowRect a tick, which costs nothing beside a frame and
// is quick enough that a resolution change is corrected while the options menu is still
// on screen.
constexpr DWORD kPollIntervalMs = 250;

struct Size {
    int width = 0;
    int height = 0;
};

bool operator==(const Size& a, const Size& b) {
    return a.width == b.width && a.height == b.height;
}

bool operator!=(const Size& a, const Size& b) { return !(a == b); }

WindowRect ToWindowRect(const RECT& r) {
    return WindowRect{ r.left, r.top, r.right, r.bottom };
}

void CenterOnItsMonitor(HWND hwnd, const RECT& window) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("WARN: [window] the monitor the game is on could not be measured "
                  "(error %lu), so the window is left where it is.", GetLastError());
        return;
    }

    const WindowRect win = ToWindowRect(window);
    const WindowRect work = ToWindowRect(info.rcWork);
    const Placement placement = CenterOnWorkArea(win, work);
    if (placement.decision == PlacementDecision::FillsWorkArea) {
        Log::Line("[window] the %dx%d window fills the %dx%d work area, so it is "
                  "fullscreen or borderless and is left where it is.",
                  win.Width(), win.Height(), work.Width(), work.Height());
        return;
    }
    if (placement.decision == PlacementDecision::AlreadyCentred) {
        Log::Line("[window] the %dx%d window is already centred at (%d, %d), so it is "
                  "left where it is.", win.Width(), win.Height(), win.left, win.top);
        return;
    }

    // SWP_ASYNCWINDOWPOS because the window belongs to the game's thread. Without it
    // SetWindowPos sends the position messages and blocks until that thread pumps, which
    // is a wait this thread has no way out of while the game is busy.
    if (!SetWindowPos(hwnd, nullptr, placement.x, placement.y, 0, 0,
                      SWP_ASYNCWINDOWPOS | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log::Line("WARN: [window] the %dx%d window could not be moved (error %lu).",
                  win.Width(), win.Height(), GetLastError());
        return;
    }

    Log::Line("[window] centred the %dx%d window at (%d, %d) on the %dx%d work area.",
              win.Width(), win.Height(), placement.x, placement.y, work.Width(),
              work.Height());
}

// What is watched for is a SIZE the window has not been placed at yet, not a position:
// the game sizes its window at startup and again whenever the resolution changes, and a
// position that moved without the size changing is the player having dragged it, which
// is theirs to decide.
DWORD WINAPI WatchThread(LPVOID) {
    Size settled;
    Size placed;

    for (;;) {
        Sleep(kPollIntervalMs);

        // Searched again every tick rather than held, for the 200x200 floor the search
        // applies: the game shrinks its own window to 160x120 while it brings the render
        // device up, and centring that moves a window nobody is looking at and then has
        // to move the real one again a moment later.
        //
        // A minimised window is skipped on top of that. It reports the size of its
        // title-bar stub off at (-32000, -32000), and SetWindowPos on one moves where it
        // restores TO, so alt-tabbing out of the game would move its window.
        const HWND hwnd = cameraunlock::os::FindGameWindow();
        RECT rect{};
        if (!hwnd || IsIconic(hwnd) || !GetWindowRect(hwnd, &rect)) {
            // Only the settle record is dropped. Forgetting what was PLACED would
            // re-centre a window the player had dragged somewhere themselves: minimising
            // and restoring does not change the size, so the two agreeing ticks pass and
            // the window is moved back, which is exactly what the size-not-position rule
            // above exists to prevent.
            settled = Size{};
            continue;
        }

        const Size now{ rect.right - rect.left, rect.bottom - rect.top };
        // Two agreeing ticks before moving anything. A window caught mid-resize would
        // otherwise be centred at an intermediate size and then again at the real one,
        // which the player sees as the window jumping twice.
        if (now != settled) {
            settled = now;
            continue;
        }
        if (now == placed) continue;

        CenterOnItsMonitor(hwnd, rect);
        // Recorded whether or not the window actually moved. A size deliberately left
        // alone, or one that could not be moved, must not be retried four times a second
        // with a log line each time.
        placed = now;
    }
}

}  // namespace

void StartWindowCentering() {
    const HANDLE thread = CreateThread(nullptr, 0, &WatchThread, nullptr, 0, nullptr);
    if (!thread) {
        Log::Line("WARN: [window] the window watcher could not start (error %lu). The "
                  "window is left wherever the game puts it.", GetLastError());
        return;
    }
    CloseHandle(thread);
}

}  // namespace BioShockInfiniteHeadTracking
