#pragma once

#include "config.h"

#include "cameraunlock/input/hotkey_poller.h"

#include <atomic>
#include <functional>

namespace BioShockInfiniteHeadTracking {

// The chord half of every binding, on the T/Y/U/G/H/J block a hand finds by touch.
// Ctrl+Shift+<letter> is avoided by games, and the fleet fixes WHICH letter carries which
// action so the same press does the same thing in every mod - reshuffling them is the one
// change AGENTS.md rules out. Named rather than written inline at the call site so a test
// can hold them to it.
//
// Ctrl+Shift+T is deliberately absent: it was the recenter chord before mods stopped
// keeping a centre of their own, and it stays unbound so muscle memory fires nothing.
constexpr int kChordToggleKey = 'Y';
constexpr int kChordCycleModeKey = 'G';
constexpr int kChordYawModeKey = 'H';
constexpr int kChordAdsModeKey = 'U';

class Hotkeys {
public:
    using Action = std::function<void()>;

    bool Start(const Config& cfg, Action onToggle,
               Action onCycleMode, Action onYawMode, Action onAdsMode);
    void Stop();

private:
    cameraunlock::input::HotkeyPoller m_poller;
    // The poller's own Stop() tests a flag and then joins, so two callers reaching it
    // together would join one std::thread twice; claiming this flag leaves exactly one of
    // them to do it. Only the init thread calls Stop() today - DLL_PROCESS_DETACH
    // deliberately does nothing - but the flag is what keeps that a safe assumption to
    // stop relying on.
    std::atomic<bool> m_started{false};
};

}  // namespace BioShockInfiniteHeadTracking
