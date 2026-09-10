#pragma once

#include "MinHook.h"

namespace BioShockInfiniteHeadTracking {

inline MH_STATUS CreateAndEnableHook(void* target, void* detour, void** original) {
    const MH_STATUS created = MH_CreateHook(target, detour, original);
    if (created != MH_OK) {
        return created;
    }
    const MH_STATUS enabled = MH_EnableHook(target);
    if (enabled != MH_OK) {
        MH_RemoveHook(target);
    }
    return enabled;
}

}  // namespace BioShockInfiniteHeadTracking
