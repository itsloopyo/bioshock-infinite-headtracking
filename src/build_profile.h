#pragma once

#include "cameraunlock/memory/pe_fingerprint.h"

#include <cstddef>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {

// Where this build keeps the engine's own object model. Pinned per build for the same
// reason the RVAs are: a patch moves a member as readily as it moves a function, and
// these are read through rather than called, so a stale one is a wrong answer rather
// than a crash. ue3_object.cpp checks the whole set once at runtime by reading the
// player controller's class name back, which is the one thing that cannot be right by
// accident.
struct Ue3Layout {
    // The name pool: a { Data, Num, Max } triple. Everything an object says about itself
    // is an index into it.
    std::uintptr_t rvaGNamesData;
    std::uintptr_t rvaGNamesNum;

    // A pool entry: its own index shifted up one with the wide-text flag in bit 0, and
    // the text itself. The text is ASCII unless that flag says otherwise.
    std::size_t offNameEntryPacked;
    std::size_t offNameEntryText;

    // UObject: the object it belongs to, its name, and its class.
    std::size_t offObjectOuter;
    std::size_t offObjectName;
    std::size_t offObjectClass;

    // UField: the next field in the class that declares it.
    std::size_t offFieldNext;

    // UStruct: the class it extends, and the head of its own field list.
    std::size_t offStructSuper;
    std::size_t offStructChildren;

    // UProperty: the byte offset of the value inside an instance. UBoolProperty adds the
    // bit of that dword the value lives in - several bools share one.
    std::size_t offPropertyOffset;
    std::size_t offBoolPropertyMask;
};

// One shipped BioShock Infinite build: its PE fingerprint and the RVAs the hooks pin to.
struct CrosshairLayout {
    std::uintptr_t rvaHudTick;
    std::uintptr_t rvaFindWidget;
    std::uintptr_t rvaGetPosition;
    std::uintptr_t rvaSetPosition;
    std::uintptr_t rvaResolveDisplayCharacter;
    std::uintptr_t rvaGetWorldMatrix3D;
    std::uintptr_t rvaGetViewMatrix3D;
    std::uintptr_t rvaGetProjectionMatrix3D;
    std::uintptr_t rvaWidgetVtable;
    std::size_t offCrosshairBinding;
};

// Append-only registry: a patch that moves RVAs gets a NEW profile added to the top of
// kKnownProfiles, never an in-place edit, so users on older builds keep matching their
// original profile by fingerprint.
struct BuildProfile {
    const char* name;
    cameraunlock::memory::PeFingerprint fingerprint;

    // RVA of APlayerController::GetPlayerViewPoint(FVector& out_Location,
    // FRotator& out_Rotation), the viewpoint accessor the scene-view builder calls.
    std::uintptr_t rvaGetPlayerViewPoint;

    // RVA of the instruction the scene-view builder returns to after its call to
    // GetPlayerViewPoint. The hook adds the head pose only for that one caller, so
    // rendering is head-tracked while everything else keeps the clean viewpoint.
    std::uintptr_t rvaSceneViewCallSite;

    // RVA of APlayerController::GetFOVAngle().
    std::uintptr_t rvaGetFovAngle;

    // RVA of the instruction the scene-view builder returns to after its call to
    // GetFOVAngle. The accessor has other callers, all game logic; only this one's
    // result describes the frame about to be drawn.
    std::uintptr_t rvaFovCallSite;

    // RVA of ULocalPlayer::CalcSceneView, which returns the FSceneView the frame is
    // rendered from. Reticle correction reads its projection matrix to use the
    // renderer's actual projection rather than an assumed field-of-view convention.
    std::uintptr_t rvaCalcSceneView;

    // Field offsets inside the engine objects the hooks read. These belong here for the
    // same reason the RVAs do: a patch moves a member as readily as it moves a function,
    // and an offset left as a file-local constant cannot differ between two profiles. A
    // build appended with correct RVAs and a moved layout would then read the wrong dword
    // and report "no camera possessed" on every frame, with nothing in the log saying so.

    // APlayerController::PlayerCamera. The front end can possess a camera too.
    std::size_t offPlayerCamera;

    // The player controller's zoom flags, and the mask covering the hold-to-aim and
    // toggle-to-aim bits. Non-zero under the mask means the sights are up.
    std::size_t offZoomFlags;
    std::uint32_t zoomFlagsMask;

    // ACamera's unzoomed field of view: the angle the camera renders at when no zoom or
    // scripted shot is holding it to another one.
    std::size_t offCameraDefaultFov;

    // APlayerController's own default field of view, returned by GetFOVAngle when the
    // controller possesses no camera.
    std::size_t offControllerDefaultFov;

    // RVA of AXPlayerController::IsInPauseScreenFlow(), the game's own answer to "is a
    // menu up". A predicate the game already asks itself: it reads the screen the
    // controller opened and the stack of screens the front end is showing, and writes
    // nothing. Called rather than reproduced, so the pause menu, the map, the gear
    // screen and the options all report themselves without this mod knowing what any of
    // them are.
    std::uintptr_t rvaIsInPauseScreenFlow;

    Ue3Layout ue3;
    CrosshairLayout crosshair;
    std::size_t offWorldInfo;
    std::size_t offWorldMenuFlags;
    std::uint32_t worldMenuMask;
};

// The profile MatchRunningProfile matched. Every caller reaches this from inside a hook,
// and no hook is installed unless the match succeeded, so it is never read before it is
// set - see InitThread, which does both in that order.
const BuildProfile& ActiveProfile();

// Most-recent build first (diagnostic primary).
extern const BuildProfile kKnownProfiles[];
extern const int kKnownProfileCount;

// Returns the profile matching the running EXE, or nullptr when no profile
// matches (mod stays dormant - no hooks installed).
const BuildProfile* MatchRunningProfile();

}  // namespace BioShockInfiniteHeadTracking
