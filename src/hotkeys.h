#pragma once

#include "config.h"

#include "cameraunlock/input/hotkey_poller.h"

#include <atomic>
#include <functional>

namespace BioShockInfiniteHeadTracking {

class Hotkeys {
public:
    using Action = std::function<void()>;

    // Registers the three key lists from the config, chords included: every key a list
    // names runs its action.
    bool Start(const Config& cfg, Action onToggle, Action onCycleMode, Action onYawMode);
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
