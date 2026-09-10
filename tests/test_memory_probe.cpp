// Characterization tests for IsReadable (src/memory_probe.h), the predicate in front of
// every read this mod makes at an address a build profile supplied rather than at an
// object the engine handed it.
//
// It is the guard that decides whether a profile whose offset no longer fits the running
// build reports "not aiming" and logs a line, or dereferences past the end of an
// allocation inside the render path. So the cases worth pinning are the ways a page stops
// being readable: never committed, committed but not accessible, a guard page, and an
// address that is fine at its start and runs off the end of its region.
//
// Real pages from the OS rather than a fake: the whole predicate is a statement about
// what VirtualQuery says, so a stub would only test itself.

#include <windows.h>

#include <cstdio>
#include <initializer_list>
#include <cstdint>

#include "memory_probe.h"
#include "build_profile.h"
#include "ue3_object.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const char* file, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s\n", file, line, what);
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

using BioShockInfiniteHeadTracking::IsReadable;
using BioShockInfiniteHeadTracking::ReadProfileField;

SIZE_T PageSize() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwPageSize;
}

// Two pages reserved together so the second can be reprotected, which splits the region
// and gives the bounds check below a real region end to run off.
struct TwoPages {
    BYTE* base = nullptr;
    SIZE_T pageSize = 0;

    TwoPages() : pageSize(PageSize()) {
        base = static_cast<BYTE*>(VirtualAlloc(nullptr, pageSize * 2, MEM_RESERVE | MEM_COMMIT,
                                               PAGE_READWRITE));
    }
    ~TwoPages() {
        if (base) VirtualFree(base, 0, MEM_RELEASE);
    }
    TwoPages(const TwoPages&) = delete;
    TwoPages& operator=(const TwoPages&) = delete;
};

void TestCommittedReadWritePagesAreReadable() {
    TwoPages pages;
    CHECK(pages.base != nullptr);
    CHECK(IsReadable(pages.base, 1));
    CHECK(IsReadable(pages.base, pages.pageSize * 2));
    CHECK(IsReadable(pages.base + pages.pageSize, pages.pageSize));
}

void TestASpanRunningPastTheRegionEndIsRefused() {
    TwoPages pages;
    CHECK(pages.base != nullptr);
    // Reprotecting the second page splits the two-page region in two, so the first page's
    // region now ends where the second begins.
    DWORD previous = 0;
    CHECK(VirtualProtect(pages.base + pages.pageSize, pages.pageSize, PAGE_NOACCESS,
                         &previous) != 0);
    CHECK(IsReadable(pages.base, pages.pageSize));
    CHECK(!IsReadable(pages.base, pages.pageSize + 1));
    CHECK(!IsReadable(pages.base + pages.pageSize - 4, 8));
}

void TestAnInaccessiblePageIsRefused() {
    TwoPages pages;
    CHECK(pages.base != nullptr);
    DWORD previous = 0;
    CHECK(VirtualProtect(pages.base, pages.pageSize, PAGE_NOACCESS, &previous) != 0);
    CHECK(!IsReadable(pages.base, 1));
}

// A guard page reads as committed and read-write, and touching one raises rather than
// returning a value. The protection bit is the only thing that separates it from an
// ordinary page, which is why the predicate tests for it.
void TestAGuardPageIsRefused() {
    TwoPages pages;
    CHECK(pages.base != nullptr);
    DWORD previous = 0;
    CHECK(VirtualProtect(pages.base, pages.pageSize, PAGE_READWRITE | PAGE_GUARD,
                         &previous) != 0);
    CHECK(!IsReadable(pages.base, 1));
}

void TestReservedButUncommittedMemoryIsRefused() {
    const SIZE_T pageSize = PageSize();
    auto* reserved = static_cast<BYTE*>(VirtualAlloc(nullptr, pageSize, MEM_RESERVE,
                                                     PAGE_READWRITE));
    CHECK(reserved != nullptr);
    CHECK(!IsReadable(reserved, 1));
    if (reserved) VirtualFree(reserved, 0, MEM_RELEASE);
}

void TestNullAndFreedAddressesAreRefused() {
    CHECK(!IsReadable(nullptr, 4));

    const SIZE_T pageSize = PageSize();
    auto* freed = static_cast<BYTE*>(VirtualAlloc(nullptr, pageSize, MEM_RESERVE | MEM_COMMIT,
                                                  PAGE_READWRITE));
    CHECK(freed != nullptr);
    CHECK(IsReadable(freed, 1));
    CHECK(VirtualFree(freed, 0, MEM_RELEASE) != 0);
    CHECK(!IsReadable(freed, 1));
}

// ReadProfileField is how the possessed camera, the zoom flags and the two field-of-view
// angles are read: at an offset a build profile supplied, on an object in the render
// path. What is pinned here is the half that only shows up on a build whose layout has
// moved - a refusal leaves the caller's own variable alone, so a profile that no longer
// fits reports "no camera" and a log line rather than dereferencing past the end of an
// allocation inside a detour.

void TestAFieldInsideTheRegionIsRead() {
    TwoPages pages;
    CHECK(pages.base != nullptr);
    *reinterpret_cast<std::uint32_t*>(pages.base + 0x240) = 0xC0FFEEu;

    std::uint32_t value = 0;
    CHECK(ReadProfileField(pages.base, 0x240, &value));
    CHECK(value == 0xC0FFEEu);
}

void TestAnOffsetPastTheRegionEndIsRefusedAndLeavesTheOutputAlone() {
    TwoPages pages;
    CHECK(pages.base != nullptr);
    DWORD previous = 0;
    CHECK(VirtualProtect(pages.base + pages.pageSize, pages.pageSize, PAGE_NOACCESS,
                         &previous) != 0);

    float value = 82.5f;
    // Straddles the region end: the first two bytes are readable and the last two are not,
    // which is the shape a profile offset lands in when a member has moved.
    CHECK(!ReadProfileField(pages.base, static_cast<std::size_t>(pages.pageSize) - 2,
                            &value));
    CHECK(value == 82.5f);

    CHECK(!ReadProfileField(pages.base, static_cast<std::size_t>(pages.pageSize) * 2,
                            &value));
    CHECK(value == 82.5f);
}

void TestAnInaccessibleObjectIsRefused() {
    TwoPages pages;
    CHECK(pages.base != nullptr);
    DWORD previous = 0;
    CHECK(VirtualProtect(pages.base, pages.pageSize, PAGE_NOACCESS, &previous) != 0);

    const void* pointer = reinterpret_cast<const void*>(0x1234u);
    CHECK(!ReadProfileField(pages.base, 0x240, &pointer));
    CHECK(pointer == reinterpret_cast<const void*>(0x1234u));
}

void TestANullObjectIsRefused() {
    std::uint32_t value = 7;
    CHECK(!ReadProfileField(nullptr, 0x240, &value));
    CHECK(value == 7u);
}

// A pointer-sized field - the possessed camera - read back as the pointer it holds,
// including the null the game writes there in the front end. Null is not a refusal:
// the read succeeded and the answer is that no camera is possessed.
void TestAPointerFieldReadsBackIncludingNull() {
    TwoPages pages;
    CHECK(pages.base != nullptr);
    *reinterpret_cast<void**>(pages.base + 0x240) = pages.base + 0x100;
    *reinterpret_cast<void**>(pages.base + 0x250) = nullptr;

    const void* camera = nullptr;
    CHECK(ReadProfileField(pages.base, 0x240, &camera));
    CHECK(camera == pages.base + 0x100);

    camera = pages.base;
    CHECK(ReadProfileField(pages.base, 0x250, &camera));
    CHECK(camera == nullptr);
}

}  // namespace


namespace ue3 = BioShockInfiniteHeadTracking::ue3;

// ue3_object.cpp reads its byte offsets from the matched build profile, and there is no
// running game here to match one. Supplied zeroed rather than linking the registry: it
// makes the boundary explicit, because the functions exercised below - the region cache,
// the guarded copy and the two dword accessors - must not consult it at all. A test that
// reached the object model would read zero offsets and fail loudly rather than
// dereference a null profile.
namespace BioShockInfiniteHeadTracking {
const BuildProfile& ActiveProfile() {
    static const BuildProfile kNone{};
    return kNone;
}
}  // namespace BioShockInfiniteHeadTracking

#define CHECKM(cond, why) Check((cond), why, __FILE__, __LINE__)

// ---- the object walk's own page gate (ue3_object.cpp) -----------------------
//
// A separate predicate from IsReadable above, with a cache in front of it, and the one
// that both shipped faults went through: the access violation of 2026-09-07 and the
// 20-second game-thread stall of 2026-09-08. Only the part of that file which does not
// need a matched build profile is exercised here - the region cache, the guarded copy and
// the two dword accessors. Everything above them needs the game's name pool.

// The bug this pins: a region that is NOT committed used to be cached with the answer
// "unreadable", and nothing could ever evict it. Only a caught fault drops the table, and
// a read inside a region already cached as unreadable never runs, so it could never fault
// its way out. The game grows its heap continuously, so the range would later hold real
// objects and every one of them would be skipped - a walk reporting thousands of objects
// visited and never finding the widget it was sent for.
void TestARegionCommittedAfterBeingSeenReservedIsReadable() {
    const std::size_t page = PageSize();
    // Reserve a span without committing it, and ask about it. That is the answer that
    // must not be remembered.
    auto* base = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, page * 4, MEM_RESERVE, PAGE_NOACCESS));
    CHECKM(base != nullptr, "reserved a span to ask about");
    CHECKM(!ue3::Readable(base, 4), "a reserved-but-uncommitted span is not readable");

    // The game now commits inside it, exactly as its allocator does.
    void* committed = VirtualAlloc(base, page, MEM_COMMIT, PAGE_READWRITE);
    CHECKM(committed == base, "committed the first page of the reserved span");
    *reinterpret_cast<std::uint32_t*>(base) = 0xA5A5A5A5u;

    CHECKM(ue3::Readable(base, 4),
          "a page committed after the range was seen reserved reads as readable");
    std::uint32_t got = 0;
    CHECKM(ue3::CopyBytes(&got, base, sizeof(got)), "and the guarded copy succeeds");
    CHECKM(got == 0xA5A5A5A5u, "with the value that was written");

    VirtualFree(base, 0, MEM_RELEASE);
}

// The cache holds sixteen entries and moves a hit to the front. Walking more distinct
// regions than that has to keep answering correctly rather than returning a neighbour's
// bounds.
void TestTheRegionCacheStaysCorrectPastItsCapacity() {
    const std::size_t page = PageSize();
    constexpr int kRegions = 24;
    std::uint8_t* blocks[kRegions] = {};
    for (int i = 0; i < kRegions; ++i) {
        blocks[i] = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        CHECKM(blocks[i] != nullptr, "committed a page for the cache-pressure walk");
        *reinterpret_cast<std::uint32_t*>(blocks[i]) = static_cast<std::uint32_t>(i);
    }
    // Two passes, so the second one is answered after every entry has been evicted at
    // least once.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < kRegions; ++i) {
            CHECKM(ue3::Readable(blocks[i], 4), "each committed page still reads as readable");
            std::uint32_t got = 0;
            CHECKM(ue3::CopyBytes(&got, blocks[i], sizeof(got)), "and copies");
            CHECKM(got == static_cast<std::uint32_t>(i), "returning its own value");
        }
    }
    // A page freed after the cache has seen it must be refused, not answered from the
    // stale entry.
    VirtualFree(blocks[0], 0, MEM_RELEASE);
    std::uint32_t sink = 0;
    CHECKM(!ue3::CopyBytes(&sink, blocks[0], sizeof(sink)),
          "a page freed after it was cached is refused rather than faulting");
    for (int i = 1; i < kRegions; ++i) {
        VirtualFree(blocks[i], 0, MEM_RELEASE);
    }
}

// Readable accepts a read-only page; a WRITE to one is an access violation. The
// crosshair's two stores are the only writes this mod makes into the game, and they were
// gated on the READ predicate, which approves PAGE_READONLY and PAGE_WRITECOPY.
//
// The page is allocated read-only from the start, so the predicate answers about it
// rather than about a cached earlier protection - see the test below for that half.
void TestWritableIsStricterThanReadable() {
    const std::size_t page = PageSize();
    auto* base = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECKM(base != nullptr, "committed a page to write through");
    *reinterpret_cast<std::uint32_t*>(base) = 0x11223344u;
    CHECKM(ue3::Readable(base, 4), "a read-write page reads");
    CHECKM(ue3::Writable(base, 4), "and writes");
    CHECKM(ue3::WriteDwordAt(base, 0, 0xDEADBEEFu), "so WriteDwordAt lands");
    CHECKM(*reinterpret_cast<std::uint32_t*>(base) == 0xDEADBEEFu, "with the right value");
    VirtualFree(base, 0, MEM_RELEASE);

    auto* ro = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, page, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY));
    CHECKM(ro != nullptr, "committed a read-only page");
    CHECKM(ue3::Readable(ro, 4), "a read-only page reads");
    std::uint32_t got = 0xFFFFFFFFu;
    CHECKM(ue3::ReadDwordAt(ro, 0, &got) && got == 0u,
          "ReadDwordAt still answers on it");

    // The write must not land, and must not fault out of the process. The address may be
    // one a freed allocation used a moment ago, so the cached protection can still say
    // "writable" - which is why the guard, not the predicate, is what decides. Either way
    // WriteDwordAt answers false and the page is unchanged.
    CHECKM(!ue3::WriteDwordAt(ro, 0, 0xDEADBEEFu), "WriteDwordAt does not write it");
    CHECKM(*reinterpret_cast<const volatile std::uint32_t*>(ro) == 0u,
          "and the page is unchanged");
    // A caught fault drops the whole cache, so the protection is re-queried from the OS
    // and the predicate now agrees on its own.
    CHECKM(!ue3::Writable(ro, 4), "the predicate refuses it once the cache is re-asked");
    VirtualFree(ro, 0, MEM_RELEASE);
}

// The region cache answers from what VirtualQuery said when the region was first seen, so
// a protection that changes afterwards is not noticed - the cache is a filter, and the
// structured-exception guard is what actually decides. What must hold either way is that
// nothing crashes and nothing is corrupted: a write the page will not take is refused by
// the predicate or caught by the guard, and in both cases the memory is unchanged.
void TestAProtectionChangeBehindTheCacheStillCannotCorrupt() {
    const std::size_t page = PageSize();
    auto* base = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECKM(base != nullptr, "committed a page");
    const std::uint32_t sentinel = 0x11223344u;
    *reinterpret_cast<std::uint32_t*>(base) = sentinel;
    // Asked while the page is still writable, so whatever the cache ends up holding, it
    // was formed before the protection changed. The answer is deliberately not asserted:
    // the cache is keyed on address, and an address a freed allocation used a moment ago
    // can still carry that allocation's protection. That IS the condition under test.
    (void)ue3::Writable(base, 4);

    DWORD previous = 0;
    CHECKM(VirtualProtect(base, page, PAGE_READONLY, &previous) != 0, "made it read-only");
    // Either outcome is correct; a crash or a changed value is not.
    ue3::WriteDwordAt(base, 0, 0xDEADBEEFu);
    CHECKM(*reinterpret_cast<std::uint32_t*>(base) == sentinel,
          "the write did not land on a page that would not take it");

    CHECKM(VirtualProtect(base, page, PAGE_READWRITE, &previous) != 0, "restored it");
    VirtualFree(base, 0, MEM_RELEASE);
}

// The offset arrives from the engine's own property record, so it is not trusted. In
// 32-bit unsigned arithmetic `offset + 4` wraps for an offset near the top of the range,
// and the bounds test then passed for an address BEFORE the object - a four-byte write
// into somebody else's heap, which is strictly worse than a fault.
void TestAWrappingFieldOffsetIsRefused() {
    const std::size_t page = PageSize();
    auto* base = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, page, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    CHECKM(base != nullptr, "committed a page to address into");
    const std::uint32_t sentinel = 0x5A5A5A5Au;
    *reinterpret_cast<std::uint32_t*>(base) = sentinel;

    std::uint32_t got = 0;
    for (const std::uint32_t offset : { 0xFFFFFFFCu, 0xFFFFFFFFu, 0x80000000u }) {
        CHECKM(!ue3::ReadDwordAt(base, offset, &got), "a wrapping offset is not read");
        CHECKM(!ue3::WriteDwordAt(base, offset, 0u), "and is not written");
    }
    CHECKM(*reinterpret_cast<std::uint32_t*>(base) == sentinel,
          "the bytes before the object are untouched");
    VirtualFree(base, 0, MEM_RELEASE);
}

int main() {
    TestCommittedReadWritePagesAreReadable();
    TestASpanRunningPastTheRegionEndIsRefused();
    TestAnInaccessiblePageIsRefused();
    TestAGuardPageIsRefused();
    TestReservedButUncommittedMemoryIsRefused();
    TestNullAndFreedAddressesAreRefused();

    TestAFieldInsideTheRegionIsRead();
    TestAnOffsetPastTheRegionEndIsRefusedAndLeavesTheOutputAlone();
    TestAnInaccessibleObjectIsRefused();
    TestANullObjectIsRefused();
    TestAPointerFieldReadsBackIncludingNull();

    TestARegionCommittedAfterBeingSeenReservedIsReadable();
    TestTheRegionCacheStaysCorrectPastItsCapacity();
    TestWritableIsStricterThanReadable();
    TestAProtectionChangeBehindTheCacheStillCannotCorrupt();
    TestAWrappingFieldOffsetIsRefused();


    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
