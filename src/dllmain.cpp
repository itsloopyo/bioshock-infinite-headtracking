#include "build_profile.h"
#include "camera_hook.h"
#include "config.h"
#include "game_crosshair.h"
#include "fov_override.h"
#include "hotkeys.h"
#include "logging.h"
#include "path_utils.h"
#include "scene_view.h"
#include "state_probe.h"
#include "tracking_runtime.h"
#include "window_center.h"

#include "MinHook.h"

#include <windows.h>

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <string>

#ifndef HEADTRACKING_VERSION
#error "HEADTRACKING_VERSION must be defined by the build (the version in CMakeLists.txt)"
#endif

namespace BioShockInfiniteHeadTracking {

namespace {

// Constructed once and never destroyed, which is deliberate and is the counterpart of
// DLL_PROCESS_DETACH doing nothing.
//
// As plain namespace-scope objects, the two that own threads have non-trivial
// destructors, and the CRT runs those immediately after DllMain returns - still holding
// the loader lock, and after ExitProcess has already terminated the tracker, hotkey,
// status and window threads at whatever instruction each happened to be on. Both join a
// thread and free heap, so either can wait forever on a lock a terminated thread still
// owns. The player sees the window vanish and the process sit in the task list. Leaking
// them at exit costs an address space that is being discarded anyway.
template <typename T>
T& NeverDestroyed() {
    static T* const instance = new T();
    return *instance;
}

// Config is all scalars and enums, so it has no destructor to run and needs no wrapper.
Config g_config;
TrackingRuntime& g_tracking = NeverDestroyed<TrackingRuntime>();
Hotkeys& g_hotkeys = NeverDestroyed<Hotkeys>();

// How the mod is doing, in the states a player's "it isn't working" report can actually
// be in. Reported on change only - see StatusThread.
enum class Liveness {
    Unknown,
    // The viewpoint accessor has not been called at all. Normal at the splash screens.
    NoFrames,
    // It is being called, but never from the scene-view call site, so nothing the mod
    // injects reaches the rendered frame.
    NotReachingTheView,
    // The rendered view is head-tracked, and no tracker is sending to it.
    NoPackets,
    // The player has switched tracking off.
    Disabled,
    Tracking,
};

const char* Describe(Liveness state, unsigned long renderDelta, unsigned long otherDelta,
                     unsigned short port, char* buf, std::size_t bufLen) {
    switch (state) {
        case Liveness::NoFrames:
            return "Status: no frames. APlayerController::GetPlayerViewPoint has not been "
                   "called at all - normal at the splash screens and the front end, but if "
                   "it stays this way once you are in a level the hook is not live.";
        case Liveness::NotReachingTheView:
            std::snprintf(buf, bufLen,
                          "Status: the viewpoint accessor ran %lu times and not once for "
                          "the scene view, so nothing the mod injects reaches the rendered "
                          "frame. The scene-view call site in the matched build profile no "
                          "longer fits this EXE.", otherDelta);
            return buf;
        case Liveness::NoPackets:
            std::snprintf(buf, bufLen,
                          "Status: %lu frames were rendered through the hook, and no "
                          "tracker packets are arriving on port %u. Point your tracker at "
                          "this PC's address and that port.", renderDelta, port);
            return buf;
        case Liveness::Disabled:
            std::snprintf(buf, bufLen,
                          "Status: tracking is switched OFF. %lu frames were rendered "
                          "through the hook and no head pose was applied to any of them. "
                          "Press End (or Ctrl+Shift+Y) to switch it back on.", renderDelta);
            return buf;
        case Liveness::Tracking:
            std::snprintf(buf, bufLen,
                          "Status: tracking. %lu frames rendered through the hook with "
                          "tracker packets arriving.", renderDelta);
            return buf;
        case Liveness::Unknown:
            break;
    }
    return "Status: unknown";
}

// Reported on CHANGE, never on a timer. A line every few seconds would be the whole log
// by the time a player finishes a chapter, and the states above are the entire
// content of one: what a reader wants is the moment tracking started, and the moment it
// stopped, which is exactly the set of transitions.
constexpr DWORD kStatusIntervalMs = 5000;

// Runs until the process exits, which is the only way this DLL is ever unloaded (see
// DllMain). There is deliberately no stop event: an object whose only purpose would be
// to end a thread that cannot be ended is a synchronisation primitive standing in for a
// comment.
DWORD WINAPI StatusThread(LPVOID) {
    unsigned long lastRender = 0;
    unsigned long lastOther = 0;
    Liveness reported = Liveness::Unknown;

    for (;;) {
        Sleep(kStatusIntervalMs);
        const unsigned long render = CameraHookCallCount();
        const unsigned long other = CameraHookNonRenderCallCount();
        const unsigned long renderDelta = render - lastRender;
        const unsigned long otherDelta = other - lastOther;
        lastRender = render;
        lastOther = other;

        Liveness state;
        if (renderDelta == 0 && otherDelta == 0) {
            state = Liveness::NoFrames;
        } else if (renderDelta == 0) {
            state = Liveness::NotReachingTheView;
        } else if (!g_tracking.IsEnabled()) {
            state = Liveness::Disabled;
        } else if (!g_tracking.IsReceiving()) {
            state = Liveness::NoPackets;
        } else {
            state = Liveness::Tracking;
        }

        if (state == reported) {
            continue;
        }
        reported = state;
        char buf[320];
        Log::Line("%s", Describe(state, renderDelta, otherDelta, g_config.udp_port, buf,
                                sizeof(buf)));
    }
}

// One line carrying the settings a "it is behaving oddly" report turns on, so the answer
// does not need the player's INI as well as their log.
void ReportConfig() {
    Log::Line("Config: port %u, freshness %d ms, smoothing local %.2f / remote %.2f, "
              "yaw %s, position %s (x %.2f, y +%.2f/-%.2f, z %.2f/-%.2f m), stock crosshair correction %s, "
              "sights %s, tracking starts %s",
              g_config.udp_port, g_config.data_freshness_ms, g_config.local_smoothing,
              g_config.remote_smoothing,
              g_config.world_space_yaw ? "world-space (horizon-locked)" : "camera-local",
              g_config.position_enabled ? "on" : "off", g_config.pos_limit_x,
              g_config.pos_limit_y, g_config.pos_limit_y_down, g_config.pos_limit_z,
              g_config.pos_limit_z_back,
              g_config.show_aim_marker ? "on" : "off", AdsModeValue(g_config.ads_mode),
              g_config.enabled_on_startup ? "enabled" : "disabled");
}

// Everything the mod does, stopped, on the one path that reaches here: the hotkeys did
// not start, so the player would have no way to switch tracking off in game and it must
// not be left on. Nothing is unhooked and MinHook is not uninitialised - see
// DisableCameraTracking.
void GoInert() {
    g_hotkeys.Stop();
    g_tracking.Stop();
    DisableCameraTracking();
    // The field of view too. It is a separate detour reading a separate value, so standing
    // the camera down leaves it rewriting every frame - a player whose hotkeys failed
    // would get a session with no head tracking, a field of view they cannot change back,
    // and a log line saying the mod had stood down.
    DisableFovOverride();
}

// Everything that touches another module runs here rather than in DllMain: resolving
// BioShockInfinite.exe, detouring it and starting threads all need the loader lock this
// DLL is holding while its entry point runs.
DWORD WINAPI InitThread(LPVOID) {
    Log::Line("BioShock Infinite Head Tracking " HEADTRACKING_VERSION " starting");

    const std::string iniPath = GetModulePath("HeadTracking.ini");
    if (iniPath.empty()) {
        Log::Line("ERROR: this mod's own folder has no name this API can express, and 8.3 "
                  "short names are switched off on the volume, so HeadTracking.ini cannot "
                  "be located. Staying dormant.");
        return 0;
    }
    if (!g_config.LoadOrCreate(iniPath.c_str())) {
        // The specific reason has already been written by the reader itself - a value out
        // of range, or the file could not be opened. This says which file and stops.
        Log::Line("ERROR: %s was not usable. Staying dormant.", iniPath.c_str());
        return 0;
    }
    ReportConfig();
    EnableStateProbe(g_config.state_probe);

    const BuildProfile* profile = MatchRunningProfile();
    if (!profile) {
        return 0;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));

    // First, and aborting on failure: it is the hook that carries the head pose, and it
    // is the one that calls MH_Initialize for the hooks below.
    const CameraHookTargets cameraTargets{ base + profile->rvaGetPlayerViewPoint,
                                           base + profile->rvaSceneViewCallSite };
    if (!InstallCameraHook(cameraTargets, g_tracking, g_config)) {
        Log::Line("ERROR: the camera hook could not be installed. Staying dormant.");
        MH_Uninitialize();
        return 0;
    }

    // None of the following hooks carries the pose, so a failure costs the reticle or the
    // field of view and each has already said so in its own words. Head tracking runs.
    InstallSceneViewHook(base + profile->rvaCalcSceneView);
    InstallGameCrosshairHook();
    const FovHookTargets fovTargets{ base + profile->rvaGetFovAngle,
                                     base + profile->rvaFovCallSite };
    InstallFovHook(fovTargets, g_config);
    g_tracking.Start(g_config);
    if (!g_hotkeys.Start(g_config,
                         [] { g_tracking.ToggleEnabled(); },
                         [] { g_tracking.CycleTrackingMode(); },
                         [] { g_tracking.ToggleYawMode(); },
                         [] { g_tracking.CycleAdsMode(); })) {
        Log::Line("ERROR: no hotkeys, so tracking could not be switched off in game. "
                  "Standing down and staying dormant.");
        GoInert();
        return 0;
    }

    const HANDLE status = CreateThread(nullptr, 0, &StatusThread, nullptr, 0, nullptr);
    if (!status) {
        // Not fatal: tracking is already running and the heartbeat only reports on it.
        Log::Line("WARN: could not start the status thread (error %lu), so the log will "
                  "carry no liveness lines. Head tracking is unaffected.", GetLastError());
    } else {
        CloseHandle(status);
    }

    StartWindowCentering();
    return 0;
}

}  // namespace

}  // namespace BioShockInfiniteHeadTracking

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    using namespace BioShockInfiniteHeadTracking;

    switch (reason) {
        case DLL_PROCESS_ATTACH: {
            DisableThreadLibraryCalls(module);

            // Beside this DLL, which the installer puts next to BioShockInfinite.exe in
            // Binaries\Win32. Opened before anything else runs: core's Open() rotates the
            // last run to HeadTracking.prev.log and truncates this one, so the file is
            // this launch and only this launch, and every line below has somewhere to go.
            const std::wstring logPath = GetModulePathW("HeadTracking.log");
            if (logPath.empty()) {
                // Nowhere to write the log and nowhere to read the INI from, so there is
                // nothing to say and no channel left to say it on but the debugger.
                OutputDebugStringW(L"BioShock Infinite Head Tracking: could not resolve its "
                                   L"own directory. Staying dormant.\n");
                return TRUE;
            }
            Log::Open(logPath);

            const HANDLE init = CreateThread(nullptr, 0, &InitThread, nullptr, 0, nullptr);
            if (!init) {
                Log::Line("ERROR: could not start the init thread (error %lu). Staying "
                          "dormant.", GetLastError());
                return TRUE;
            }
            CloseHandle(init);
            return TRUE;
        }

        case DLL_PROCESS_DETACH:
            // Process exit, always. BioShockInfinite.exe statically imports XINPUT1_3.dll,
            // so the loader holds a reference on this module that nothing can release:
            // an explicit FreeLibrary drops only a reference its caller added and cannot
            // unmap us. That is what makes doing nothing here correct rather than lazy.
            //
            // Nothing is joined, unhooked or uninitialised, because all three would run
            // under the loader lock this entry point is holding. MH_Uninitialize suspends
            // every thread in the process and rewrites their instruction pointers, which
            // deadlocks against a thread waiting on that same lock; joining the tracker
            // or hotkey threads deadlocks against whichever of them is inside a Win32
            // call that takes it. At exit every other thread is already gone anyway, so
            // there is nothing to join and an address space about to be discarded.
            //
            // Not even the log. Closing it takes the logging mutex, and by this point the
            // process is tearing down: a thread terminated mid-line still owns that mutex
            // and this would wait on it forever, holding the loader lock, which the player
            // sees as the window vanishing and the process never leaving the task list.
            // The lines are already on disk - every one is flushed as it is written.
            return TRUE;
    }
    return TRUE;
}
