#include "ads_state.h"

#include "build_profile.h"
#include "logging.h"
#include "memory_probe.h"

#include <cstddef>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {

namespace {

// Whether the sights are up, read as a flag on the player controller.
//
// The offset and the mask both live in the build profile rather than here, because a
// patch moves a member as readily as it moves a function and a file-local constant
// cannot differ between two profiles. The mask covers the hold-to-aim and the
// toggle-to-aim bit together: a player on the toggle binding never sets the hold bit at
// all, so testing one alone would report the sights down for half the audience.
//
// Verified on the steam-win32-20220511 build by watching the flag through the log while
// aiming on both bindings.

// Said once. A profile whose zoom offset does not fit this build would otherwise leave
// the sights permanently down with nothing in the log, and `paused` - the shipped
// default - would look like it had simply stopped working.
void ReportUnreadableOnce(std::size_t offset) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("WARN: the zoom flags at PlayerController+0x%X are not readable, so the "
              "sights are reported down on every frame. Head tracking is unaffected; what "
              "is lost is what it does while you aim.", static_cast<unsigned>(offset));
}

}  // namespace

bool PlayerIsAiming(const void* playerController) {
    if (!playerController) {
        return false;
    }
    const std::size_t offset = ActiveProfile().offZoomFlags;
    std::uint32_t flags = 0;
    // Through the page check rather than straight off the pointer: this is a fixed offset
    // a build profile supplies, and the contract in ads_state.h is that an unreadable or
    // absent flag reports "not aiming" - the direction that fails toward the game's own
    // camera. Without the check a profile whose offset landed past the end of the
    // allocation would take the game down inside the render path.
    if (!ReadProfileField(playerController, offset, &flags)) {
        ReportUnreadableOnce(offset);
        return false;
    }
    return (flags & ActiveProfile().zoomFlagsMask) != 0;
}

}  // namespace BioShockInfiniteHeadTracking
