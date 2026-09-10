#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace BioShockInfiniteHeadTracking {

// Whether a span of the game's memory can be read right now.
//
// Every read this mod makes of an address a build profile supplied - rather than of an
// object the engine has just handed us - goes through here first. A profile whose offset
// landed past the end of an allocation would otherwise take the game down from inside the
// render path, and the contract each caller documents is that an unreadable address is
// answered rather than dereferenced.
//
// It answers for the instant it runs. A caller that walks structures the game is free to
// release on its own threads needs a guarded read as well as this check, which is why
// ue3_object.cpp keeps its own cached-region variant on top of the same predicate rather
// than calling this one: the object walk asks about millions of words and cannot afford a
// system call each time.
inline bool IsReadable(const void* p, std::size_t bytes) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT) {
        return false;
    }
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & readable) == 0 || (mbi.Protect & PAGE_GUARD) != 0) {
        return false;
    }
    const auto* regionEnd = static_cast<const std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    return static_cast<const std::uint8_t*>(p) + bytes <= regionEnd;
}

// One field of a game object, at an offset a build profile supplied.
//
// The three places this mod reads such a field - the possessed camera, the zoom flags and
// the two field-of-view angles - all sit in the render path, and all of them would take
// the game down from inside a detour if a profile's offset landed past the end of the
// allocation. So the check above is not a thing each of them remembers to do: it is the
// only way any of them reads. False leaves `out` untouched, and every caller turns that
// into the answer that fails towards the game's own camera.
template <typename T>
bool ReadProfileField(const void* object, std::size_t offset, T* out) {
    if (!object || !out) {
        return false;
    }
    const auto* address = static_cast<const std::uint8_t*>(object) + offset;
    if (!IsReadable(address, sizeof(T))) {
        return false;
    }
    // memcpy rather than a cast through T*: an offset read out of the game's own property
    // records carries no alignment guarantee, and this is a copy of foreign bytes rather
    // than an object of ours to be dereferenced.
    std::memcpy(out, address, sizeof(T));
    return true;
}

}  // namespace BioShockInfiniteHeadTracking
