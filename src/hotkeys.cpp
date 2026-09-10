#include "hotkeys.h"

#include "logging.h"

#include "cameraunlock/input/chord_hotkeys.h"

#include <exception>

namespace BioShockInfiniteHeadTracking {

bool Hotkeys::Start(const Config& cfg, Action onToggle,
                    Action onCycleMode, Action onYawMode, Action onAdsMode) {
    if (m_started.load(std::memory_order_acquire)) return true;

    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;

    // Nav-cluster keys are suppressed while Ctrl+Shift is held so the chord
    // path is the sole trigger for Ctrl+Shift+<nav> combos - a single keypress
    // never fires an action twice.
    m_poller.SetToggleKey(cfg.vk_toggle, NavGuarded(onToggle));
    m_poller.AddHotkey(cfg.vk_cycle_mode, NavGuarded(onCycleMode));
    m_poller.AddHotkey(cfg.vk_yaw_mode, NavGuarded(onYawMode));
    m_poller.AddHotkey(cfg.vk_ads_mode, NavGuarded(onAdsMode));

    // Chord alternatives (Ctrl+Shift+Y / Ctrl+Shift+G / Ctrl+Shift+H / Ctrl+Shift+U)
    // on the same poller; ChordGuarded gates each action on the modifier state.
    if (cfg.chord_toggle)     m_poller.AddHotkey(kChordToggleKey, ChordGuarded(std::move(onToggle)));
    if (cfg.chord_cycle_mode) m_poller.AddHotkey(kChordCycleModeKey, ChordGuarded(std::move(onCycleMode)));
    if (cfg.chord_yaw_mode)   m_poller.AddHotkey(kChordYawModeKey, ChordGuarded(std::move(onYawMode)));
    if (cfg.chord_ads_mode)   m_poller.AddHotkey(kChordAdsModeKey, ChordGuarded(std::move(onAdsMode)));

    // The poller rethrows rather than failing silently when the thread cannot be
    // created. This entry point is reached from a __stdcall thread procedure, where an
    // escaping exception is std::terminate: the game would vanish during startup with
    // the log ending on an unrelated line. Caught here so the reason is written down
    // and the caller can tear tracking back down and stay dormant.
    try {
        if (!m_poller.Start(16)) {
            Log::Line("ERROR: HotkeyPoller failed to start");
            return false;
        }
    } catch (const std::exception& e) {
        Log::Line("ERROR: HotkeyPoller failed to start: %s", e.what());
        return false;
    }

    Log::Line("Hotkeys: toggle=0x%02X cyclemode=0x%02X yawmode=0x%02X adsmode=0x%02X",
              cfg.vk_toggle, cfg.vk_cycle_mode, cfg.vk_yaw_mode, cfg.vk_ads_mode);

    m_started.store(true, std::memory_order_release);
    return true;
}

void Hotkeys::Stop() {
    if (m_started.exchange(false, std::memory_order_acq_rel)) {
        m_poller.Stop();
    }
}

}  // namespace BioShockInfiniteHeadTracking
