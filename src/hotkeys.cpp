#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

#include <exception>
#include <stdexcept>
#include <string>

namespace BioShockInfiniteHeadTracking {

namespace {

// The table read each list through the hotkey codec, which keeps the default for a value
// it cannot read, so every list here parses.
std::vector<cameraunlock::input::KeyBinding> Bindings(const char* key, const std::string& list) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) {
        throw std::logic_error(std::string("[Hotkeys] ") + key + "=" + list + " did not parse: " + parsed.error);
    }
    return parsed.bindings;
}

}  // namespace

bool Hotkeys::Start(const Config& cfg, Action onToggle, Action onCycleMode,
                    Action onYawMode) {
    if (m_started.load(std::memory_order_acquire)) return true;

    // The poller rethrows rather than failing silently when the thread cannot be
    // created. This entry point is reached from a __stdcall thread procedure, where an
    // escaping exception is std::terminate: the game would vanish during startup with
    // the log ending on an unrelated line. Caught here so the reason is written down
    // and the caller can tear tracking back down and stay dormant.
    try {
        using cameraunlock::input::RegisterKeyBindings;
        RegisterKeyBindings(m_poller, Bindings("ToggleKey", cfg.toggle_key), std::move(onToggle));
        RegisterKeyBindings(m_poller, Bindings("CycleTrackingModeKey", cfg.cycle_tracking_mode_key),
                            std::move(onCycleMode));
        RegisterKeyBindings(m_poller, Bindings("YawModeKey", cfg.yaw_mode_key), std::move(onYawMode));
        if (!m_poller.Start(16)) {
            Log::Line("ERROR: HotkeyPoller failed to start");
            return false;
        }
    } catch (const std::exception& e) {
        Log::Line("ERROR: HotkeyPoller failed to start: %s", e.what());
        return false;
    }

    Log::Line("Hotkeys: toggle %s; cycle tracking mode %s; yaw mode %s", cfg.toggle_key.c_str(),
              cfg.cycle_tracking_mode_key.c_str(), cfg.yaw_mode_key.c_str());

    m_started.store(true, std::memory_order_release);
    return true;
}

void Hotkeys::Stop() {
    if (m_started.exchange(false, std::memory_order_acq_rel)) {
        m_poller.Stop();
    }
}

}  // namespace BioShockInfiniteHeadTracking
