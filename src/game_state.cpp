#include "game_state.h"

#include "build_profile.h"
#include "game_module.h"
#include "logging.h"
#include "memory_probe.h"
#include "state_probe.h"

#include <windows.h>

#include <cstddef>

namespace BioShockInfiniteHeadTracking {

namespace {

// Said once. Without it a profile whose camera offset does not fit this build reports
// "no camera possessed" on every frame - a line that is true about what the mod decided
// and wrong about why, and the one thing a reader would then chase is the front end.
void ReportUnreadableOnce(std::size_t offset) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("WARN: the possessed camera at PlayerController+0x%X is not readable, so "
              "every frame is reported as having no camera and no head pose reaches the "
              "view. The object layout in this build profile does not fit this EXE.",
              static_cast<unsigned>(offset));
}

// AXPlayerController::IsInPauseScreenFlow(), __thiscall with no arguments, modelled as
// __fastcall with a dummy edx. Returns non-zero while a menu is up.
//
// The game's own answer, called rather than reproduced. It reads the screen the
// controller opened and the stack of screens the front end has pushed, so the pause menu,
// the map, the gear screen and the options all report themselves - and it writes nothing,
// which is what makes it safe to ask once per rendered frame from inside a detour.
using IsInPauseScreenFlow_t = int(__fastcall*)(const void* thisptr, void* edx);

IsInPauseScreenFlow_t PauseScreenFlow() {
    static const IsInPauseScreenFlow_t fn =
        GameFunction<IsInPauseScreenFlow_t>(ActiveProfile().rvaIsInPauseScreenFlow);
    return fn;
}

}  // namespace

const char* Describe(GameplayState state) {
    switch (state) {
        case GameplayState::Playing:  return "playing";
        case GameplayState::NoCamera: return "no camera possessed";
        case GameplayState::Menu:     return "front-end menu";
        case GameplayState::Paused:   return "paused (menu or pause screen)";
    }
    return "unknown";
}

const void* GetPlayerCamera(const void* playerController) {
    if (!playerController) {
        return nullptr;
    }
    // Through the page check, on the same terms as the zoom flags: the offset comes from
    // the build profile rather than from the engine, and a profile whose offset landed
    // past the end of the allocation would take the game down from inside the detour.
    // Refused reads as no camera, which suppresses tracking - the direction that fails
    // towards the game's own view.
    const std::size_t offset = ActiveProfile().offPlayerCamera;
    const void* camera = nullptr;
    if (!ReadProfileField(playerController, offset, &camera)) {
        ReportUnreadableOnce(offset);
        return nullptr;
    }
    return camera;
}

GameplayState GetGameplayState(const void* playerController) {
    StateProbeTick(playerController);

    if (!GetPlayerCamera(playerController)) {
        return GameplayState::NoCamera;
    }
    const auto& profile = ActiveProfile();
    const void* worldInfo = nullptr;
    std::uint32_t menuFlags = 0;
    if (!ReadProfileField(playerController, profile.offWorldInfo, &worldInfo) ||
        !ReadProfileField(worldInfo, profile.offWorldMenuFlags, &menuFlags)) {
        static bool reported = false;
        if (!reported) {
            Log::Line("ERROR: cannot read WorldInfo menu state. Head tracking suspended.");
            reported = true;
        }
        return GameplayState::Paused;
    }
    // The front end possesses an XCamera but has no PauseScreenFlow.
    if ((menuFlags & profile.worldMenuMask) != 0) {
        return GameplayState::Menu;
    }
    // The pause menu builds the scene view at the full frame rate, so without this the
    // head pose would keep swinging the camera around behind it while the player reads a
    // menu, and the frame clock would hand the first frame back a menu's worth of time.
    // Not null-tested. GameFunction answers null only when GetModuleHandle fails for the
    // EXE this DLL is loaded into, which cannot happen, and no hook is installed at all
    // unless a build profile matched first. Treating it as "not paused" would swing the
    // camera behind every menu for the whole session with nothing in the log to say why.
    if (PauseScreenFlow()(playerController, nullptr) != 0) {
        return GameplayState::Paused;
    }
    return GameplayState::Playing;
}

}  // namespace BioShockInfiniteHeadTracking
