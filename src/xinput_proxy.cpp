// The shim half of the mod: BioShockInfinite.exe statically imports XINPUT1_3.dll, so a
// DLL of that name beside the exe is loaded before the system copy and its DllMain runs
// during the game's own startup. That is the whole loading mechanism - there is no ASI
// loader and nothing to vendor.
//
// The game imports ORDINALS 2 and 3, not names (verified against the shipped exe's
// import table), which is why xinput1_3.def pins every ordinal to the number the system
// DLL uses. A .def that exported the same names on different ordinals would satisfy a
// linker and fail the game at load.
//
// Every export is forwarded to the genuine DLL so a controller keeps working. The real
// one lives in the system directory - the game ships no copy of its own - and
// GetSystemDirectory in a 32-bit process on 64-bit Windows resolves to SysWOW64, which
// is the 32-bit build this proxy has to chain to.

#include "logging.h"

#include <windows.h>

#include <mutex>
#include <string>

namespace {

// The same alias every other translation unit reads Log::Line through. Spelled here
// because the forwarders below sit outside the mod's namespace, where logging.h's own
// alias is not in scope.
namespace Log = ::cameraunlock::logging;

HMODULE g_real = nullptr;
std::once_flag g_loadOnce;

void LoadRealXInput() {
    char sysDir[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryA(sysDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        Log::Line(
            "ERROR: GetSystemDirectory failed (error %lu), so the real xinput1_3.dll "
            "cannot be found. Controller input is dead until this mod is uninstalled.",
            GetLastError());
        return;
    }
    std::string path(sysDir, n);
    path += "\\xinput1_3.dll";

    g_real = LoadLibraryA(path.c_str());
    if (!g_real) {
        Log::Line(
            "ERROR: could not load %s (error %lu). Controller input is dead until this "
            "mod is uninstalled. Install the DirectX End-User Runtime, which is what "
            "puts xinput1_3.dll on the system.",
            path.c_str(), GetLastError());
    }
}

// A system DLL that loaded but is missing an export we forward. Said once per export
// rather than swallowed: the forwarder below then answers "no pad in that slot" for the
// rest of the session, and without a line here the player has a controller that half
// works, this mod as the obvious suspect, and nothing in the log to clear it. Replacement
// xinput1_3.dll builds do exist - Wine and Proton layouts, some controller remappers -
// and they are exactly the ones that omit the unnamed ordinals.
void ReportMissingExport(const char* what) {
    Log::Line("WARN: the system xinput1_3.dll has no %s, so that call is answered with "
              "'no controller connected' for the rest of this session. Head tracking is "
              "unaffected.", what);
}

// Resolved on first use rather than in DllMain: this runs under the loader lock while
// the game's own imports are being bound, and LoadLibrary there is the deadlock the
// whole init-thread structure exists to avoid.
//
// Each forwarder caches what this returns, so the lookup happens once per export rather
// than once per call. GetProcAddress takes the loader's own lock, and the game polls the
// pad every frame: that is a loader-lock acquisition on the game's input path, several
// times per frame, for an answer that cannot change.
FARPROC Resolve(const char* name) {
    std::call_once(g_loadOnce, LoadRealXInput);
    if (!g_real) {
        return nullptr;
    }
    FARPROC fn = GetProcAddress(g_real, name);
    if (!fn) {
        ReportMissingExport(name);
    }
    return fn;
}

FARPROC ResolveOrdinal(WORD ordinal) {
    std::call_once(g_loadOnce, LoadRealXInput);
    if (!g_real) {
        return nullptr;
    }
    FARPROC fn = GetProcAddress(g_real, MAKEINTRESOURCEA(ordinal));
    if (!fn) {
        char what[64];
        _snprintf_s(what, sizeof(what), _TRUNCATE, "export at ordinal %u",
                    static_cast<unsigned>(ordinal));
        ReportMissingExport(what);
    }
    return fn;
}

// What every forwarder returns when the real DLL is not there. Not a silent zero: a
// caller told "success" with an untouched output struct reads whatever was on its stack
// as controller state. ERROR_DEVICE_NOT_CONNECTED is the documented "no pad in that
// slot" answer, which every XInput caller already handles.
constexpr DWORD kNotConnected = 1167;  // ERROR_DEVICE_NOT_CONNECTED

}  // namespace

// Struct pointers are forwarded as void*: nothing here inspects them, so their layout is
// irrelevant to the proxy and declaring them would drag in the XInput headers for no
// gain.
extern "C" {

DWORD WINAPI XInputGetState(DWORD userIndex, void* state) {
    using Fn = DWORD(WINAPI*)(DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetState"));
    return fn ? fn(userIndex, state) : kNotConnected;
}

DWORD WINAPI XInputSetState(DWORD userIndex, void* vibration) {
    using Fn = DWORD(WINAPI*)(DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputSetState"));
    return fn ? fn(userIndex, vibration) : kNotConnected;
}

DWORD WINAPI XInputGetCapabilities(DWORD userIndex, DWORD flags, void* capabilities) {
    using Fn = DWORD(WINAPI*)(DWORD, DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetCapabilities"));
    return fn ? fn(userIndex, flags, capabilities) : kNotConnected;
}

void WINAPI XInputEnable(BOOL enable) {
    using Fn = void(WINAPI*)(BOOL);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputEnable"));
    if (fn) {
        fn(enable);
    }
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD userIndex, void* renderGuid,
                                             void* captureGuid) {
    using Fn = DWORD(WINAPI*)(DWORD, void*, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetDSoundAudioDeviceGuids"));
    return fn ? fn(userIndex, renderGuid, captureGuid) : kNotConnected;
}

DWORD WINAPI XInputGetBatteryInformation(DWORD userIndex, BYTE devType, void* battery) {
    using Fn = DWORD(WINAPI*)(DWORD, BYTE, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetBatteryInformation"));
    return fn ? fn(userIndex, devType, battery) : kNotConnected;
}

DWORD WINAPI XInputGetKeystroke(DWORD userIndex, DWORD reserved, void* keystroke) {
    using Fn = DWORD(WINAPI*)(DWORD, DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(Resolve("XInputGetKeystroke"));
    return fn ? fn(userIndex, reserved, keystroke) : kNotConnected;
}

// Ordinals 100-103 are exported without names by the system DLL. Steam's overlay and
// several controller tools bind them by ordinal, so they are forwarded the same way -
// by ordinal, since there is no name to ask for.
DWORD WINAPI XInputGetStateEx(DWORD userIndex, void* state) {
    using Fn = DWORD(WINAPI*)(DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(ResolveOrdinal(100));
    return fn ? fn(userIndex, state) : kNotConnected;
}

DWORD WINAPI XInputWaitForGuideButton(DWORD userIndex, DWORD flags, void* buffer) {
    using Fn = DWORD(WINAPI*)(DWORD, DWORD, void*);
    static const auto fn = reinterpret_cast<Fn>(ResolveOrdinal(101));
    return fn ? fn(userIndex, flags, buffer) : kNotConnected;
}

DWORD WINAPI XInputCancelGuideButtonWait(DWORD userIndex) {
    using Fn = DWORD(WINAPI*)(DWORD);
    static const auto fn = reinterpret_cast<Fn>(ResolveOrdinal(102));
    return fn ? fn(userIndex) : kNotConnected;
}

DWORD WINAPI XInputPowerOffController(DWORD userIndex) {
    using Fn = DWORD(WINAPI*)(DWORD);
    static const auto fn = reinterpret_cast<Fn>(ResolveOrdinal(103));
    return fn ? fn(userIndex) : kNotConnected;
}

}  // extern "C"
