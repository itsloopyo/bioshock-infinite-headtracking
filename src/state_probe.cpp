#include "state_probe.h"

#include "build_profile.h"
#include "game_state.h"
#include "logging.h"
#include "memory_probe.h"
#include "ue3_object.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace BioShockInfiniteHeadTracking {

namespace {

std::atomic<bool> g_enabled{false};

// How much of each object is dumped. The controller's own fields and the camera's both
// fit inside this on the build measured, and a wider window buries the diff.
constexpr std::size_t kDumpBytes = 0x600;

// One dump every two seconds is enough to catch a state the player holds for a moment
// (a pause screen, a loading transition) without the log outrunning the diff.
constexpr unsigned long long kIntervalMs = 2000;
unsigned long long g_lastMs = 0;

void DumpObject(const char* label, const std::uint8_t* base, std::size_t bytes) {
    Log::Line("[probe] %s @ 0x%p", label, base);
    char line[256];
    for (std::size_t off = 0; off < bytes; off += 32) {
        int n = std::snprintf(line, sizeof(line), "[probe]  +%03X", static_cast<int>(off));
        for (int i = 0; i < 8; ++i) {
            // Guarded, not just region-tested. The readability check ran once for the
            // whole 0x600 span before the loop, and this dumps a live engine object that
            // a level transition can free while the 48 lines are still being written -
            // which is the shape that crashed the game once already.
            std::uint32_t v = 0;
            const bool got = ue3::CopyBytes(&v, base + off + i * 4, sizeof(v));
            if (got) {
                n += std::snprintf(line + n, sizeof(line) - static_cast<std::size_t>(n),
                                   " %08X", v);
            } else {
                n += std::snprintf(line + n, sizeof(line) - static_cast<std::size_t>(n),
                                   " ????????");
            }
        }
        Log::Line("%s", line);
    }
}

unsigned long long NowMs() {
    return GetTickCount64();
}

}  // namespace

void EnableStateProbe(bool on) {
    g_enabled.store(on, std::memory_order_relaxed);
    if (on) {
        Log::Line("[probe] state probe on: dumping the player controller every %llums. "
                  "Walk the game through menu, gameplay and pause, then diff the dumps.",
                  kIntervalMs);
    }
}

bool StateProbeEnabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

void StateProbeTick(const void* playerController) {
    if (!g_enabled.load(std::memory_order_relaxed) || !playerController) {
        return;
    }
    const unsigned long long now = NowMs();
    if (now - g_lastMs < kIntervalMs) {
        return;
    }
    g_lastMs = now;

    const auto* pc = static_cast<const std::uint8_t*>(playerController);
    if (!IsReadable(pc, kDumpBytes)) {
        Log::Line("[probe] player controller at 0x%p is not readable for 0x%X bytes",
                  pc, static_cast<unsigned>(kDumpBytes));
        return;
    }
    DumpObject("PlayerController", pc, kDumpBytes);

    const auto* camera = static_cast<const std::uint8_t*>(GetPlayerCamera(playerController));
    if (camera && IsReadable(camera, kDumpBytes)) {
        DumpObject("PlayerCamera", camera, kDumpBytes);
    } else {
        Log::Line("[probe] PlayerCamera (+0x%X) is %s",
                  static_cast<unsigned>(ActiveProfile().offPlayerCamera),
                  camera ? "not readable" : "null");
    }
}

}  // namespace BioShockInfiniteHeadTracking
