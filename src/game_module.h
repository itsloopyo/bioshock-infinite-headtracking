#pragma once

#include <windows.h>

#include <cstdint>

namespace BioShockInfiniteHeadTracking {

// The game's own EXE, and the functions inside it the build profile pins by RVA.
//
// ImageBase is 0x400000 with no ASLR on the shipped build, so the base is a constant in
// practice - but it is asked for rather than assumed, because a relocated image would
// otherwise send every resolved call into whatever now sits at that address.

constexpr const char* kGameModuleName = "BioShockInfinite.exe";

inline std::uintptr_t GameModuleBase() {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleA(kGameModuleName));
}

// A game function the active build profile names by RVA, or null when the module is not
// loaded. Callers hold the result in a function-local static: the address cannot change
// for the life of the process, and resolving it per frame would put a module lookup on
// the render path.
//
// Fn is the calling convention the game's own code uses, modelled for a detour - see the
// using-declarations at each call site, which are what say how the function is called.
template <typename Fn>
Fn GameFunction(std::uintptr_t rva) {
    const std::uintptr_t base = GameModuleBase();
    return base ? reinterpret_cast<Fn>(base + rva) : nullptr;
}

}  // namespace BioShockInfiniteHeadTracking
