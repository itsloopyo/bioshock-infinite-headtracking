#include "build_profile.h"

#include "game_module.h"
#include "logging.h"

#include <windows.h>

namespace BioShockInfiniteHeadTracking {

// steam-win32-20220511: Steam retail build, EXE linked 2022-05-11. ImageBase 0x400000,
// no ASLR, so every RVA below is a file VA minus 0x400000.
//
// The numbers are the addresses and member offsets this mod's hooks bind to on that one
// shipped build, recorded so a maintainer can re-derive them and so a patched build gets
// a NEW profile rather than an edit. Each was read out of the running process and
// confirmed against what the mod does with it - the log line each hook writes on install
// is the check. What the game does internally around them is the game's business and is
// deliberately not recorded here.
//
//   GetPlayerViewPoint  the viewpoint accessor, __thiscall with two out-parameters on
//                       the stack (location first, rotation second). 36 call sites, of
//                       which the scene-view one is the only one this mod touches; the
//                       controller's own cached viewpoint is written from the callee's
//                       locals, so the head pose added afterwards never reaches it.
//   scene-view caller   the return address inside CalcSceneView after that call. This is
//                       what makes the injection render-only.
//   GetFOVAngle         the field-of-view accessor: the camera's angle when a camera is
//                       possessed, the controller's own default when none is.
//   FOV call site       the return address inside CalcSceneView after its GetFOVAngle
//                       call.
//   CalcSceneView       returns the FSceneView the frame is drawn from.
//   PlayerCamera        the camera the controller possesses, including the front end.
//   zoom flags + mask   the controller dword and the bits that are set while the sights
//                       are up. The mask covers both the hold-to-aim and toggle-to-aim
//                       bits, so either binding reports aiming.
//   camera default FOV  the camera's unzoomed angle.
//   controller default  the controller's own default angle.
//   IsInPauseScreenFlow the controller's own menu predicate, reached from its native
//                       registry entry: the exec thunk at 0x4FDC90 evaluates no
//                       parameters and calls this implementation with the controller in
//                       ecx, so it takes no arguments and returns a UBOOL.
//   the UE3 layout      the name pool and the object/field/property offsets the reticle
//                       work reads the game's own crosshair widget through.
//
// The fields are positional - C++17 has no designated initialisers - so each is named in
// a trailing comment. Appending a profile means matching those names against
// BuildProfile's own order, and a pair swapped silently between two same-typed neighbours
// (the two call sites, the two default-FOV offsets) is a mod that hooks the wrong caller
// or reads the wrong angle with nothing in the log to say so.
static const BuildProfile kSteamProfile_20220511 = {
    "steam-win32-20220511",
    { 0x627BE455u, 0x0124F000u, 0x011590C3u },  // fingerprint: stamp / size / checksum
    0x1E1350u,                                  // rvaGetPlayerViewPoint
    0x26351Cu,                                  // rvaSceneViewCallSite
    0x1D73A0u,                                  // rvaGetFovAngle
    0x26358Fu,                                  // rvaFovCallSite
    0x2633B0u,                                  // rvaCalcSceneView
    0x240u,                                     // offPlayerCamera
    0x690u,                                     // offZoomFlags
    0x30000u,                                   // zoomFlagsMask
    0x3D0u,                                     // offCameraDefaultFov
    0x284u,                                     // offControllerDefaultFov
    0x589530u,                                  // rvaIsInPauseScreenFlow
    {
        0xF9DFECu, 0xF9DFF0u,     // GNames: rvaGNamesData, rvaGNamesNum
        0x08u, 0x10u,             // FNameEntry: offNameEntryPacked, offNameEntryText
        0x14u, 0x18u, 0x20u,      // UObject: offObjectOuter, offObjectName, offObjectClass
        0x28u,                    // UField: offFieldNext
        0x34u, 0x38u,             // UStruct: offStructSuper, offStructChildren
        0x48u, 0x58u,             // UProperty: offPropertyOffset, offBoolPropertyMask
    },
    {0x753A00u, 0x727E10u, 0x450D80u, 0x453610u, 0xA778E0u,
     0xA77660u, 0xA77730u, 0xA776E0u, 0xE3FAA8u, 0x640u},
    0xA4u,                                     // offWorldInfo: AActor::WorldInfo
    0x204u,                                    // offWorldMenuFlags: AWorldInfo::bIsMenuLevel
    0x1000u,                                   // worldMenuMask
};

const BuildProfile kKnownProfiles[] = {
    kSteamProfile_20220511,
};
const int kKnownProfileCount = static_cast<int>(sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]));

namespace {
// Set once, by MatchRunningProfile, before any hook exists to read it.
const BuildProfile* g_active = nullptr;
}  // namespace

const BuildProfile& ActiveProfile() {
    return *g_active;
}

const BuildProfile* MatchRunningProfile() {
    HMODULE hExe = GetModuleHandleA(kGameModuleName);
    if (!hExe) {
        Log::Line("ERROR: %s module not found for fingerprinting", kGameModuleName);
        return nullptr;
    }

    cameraunlock::memory::PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(hExe, running)) {
        Log::Line("ERROR: could not read PE fingerprint of BioShockInfinite.exe");
        return nullptr;
    }

    // The running fingerprint goes in the log whatever happens next. A user on a patched
    // build sends this file and nothing else, and without these three numbers the new
    // profile cannot be written from it - which is a round trip on every game patch.
    Log::Line("BioShockInfinite.exe fingerprint: TimeDateStamp 0x%08X, SizeOfImage "
              "0x%08X, CheckSum 0x%08X", running.TimeDateStamp, running.SizeOfImage,
              running.CheckSum);

    for (int i = 0; i < kKnownProfileCount; ++i) {
        const BuildProfile& p = kKnownProfiles[i];
        if (running.Matches(p.fingerprint)) {
            Log::Line("Build profile matched: %s", p.name);
            g_active = &p;
            return &p;
        }
        Log::Line("  not %s (0x%08X / 0x%08X / 0x%08X)", p.name, p.fingerprint.TimeDateStamp,
                  p.fingerprint.SizeOfImage, p.fingerprint.CheckSum);
    }

    using cameraunlock::memory::ClassifyMismatch;
    using cameraunlock::memory::FingerprintMismatch;
    const BuildProfile& primary = kKnownProfiles[0];
    switch (ClassifyMismatch(running, primary.fingerprint)) {
        case FingerprintMismatch::Newer:
            Log::Line("Unrecognised BioShock Infinite build (newer than %s). "
                      "Check the releases page for an updated mod. Staying dormant.",
                      primary.name);
            break;
        case FingerprintMismatch::Older:
            Log::Line("Unrecognised BioShock Infinite build (older than %s). "
                      "Let the store finish updating. Staying dormant.", primary.name);
            break;
        case FingerprintMismatch::Differs:
            Log::Line("BioShockInfinite.exe is tampered/repacked (fingerprint differs). "
                      "Staying dormant.");
            break;
    }
    return nullptr;
}

}  // namespace BioShockInfiniteHeadTracking
