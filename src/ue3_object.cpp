#include "ue3_object.h"

#include "build_profile.h"
#include "logging.h"

#include <windows.h>

#include <cstring>

namespace BioShockInfiniteHeadTracking {
namespace ue3 {

namespace {

const Ue3Layout& L() {
    return ActiveProfile().ue3;
}

std::uintptr_t ExeBase() {
    static const std::uintptr_t base =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleA("BioShockInfinite.exe"));
    return base;
}

// One VirtualQuery serves every read inside the region it describes, which is what makes
// walking an object graph affordable: a query per word would be a system call per word.
//
// A single cached region is not enough, and the difference is not marginal. One IsObject
// asks about the candidate, its class, its class's class and the name pool's array, which
// live in four different regions, so a one-entry cache misses on every one of them and
// the walk pays four system calls per candidate pointer. Measured in game that way: 13,019
// objects in 20.19 seconds on the game thread, 1.55 ms each, which the player sees as the
// game hanging and Windows logs as AppHangTransient. The working set is those few regions
// and nothing more, so sixteen entries hold all of it.
struct Region {
    const std::uint8_t* start = nullptr;
    const std::uint8_t* end = nullptr;
    bool readable = false;
    bool writable = false;
};

// The widest byte offset a property record is believed when it names one. UE3 objects run
// to a few kilobytes - the crosshair widget is 0x128 - so anything past this is a garbage
// offset read out of a layout that has moved, not a field.
constexpr std::uint64_t kMaxFieldSpan = 0x10000;

constexpr int kRegionCacheEntries = 16;
Region g_regions[kRegionCacheEntries];
int g_regionCount = 0;

void ForgetRegions() {
    g_regionCount = 0;
}

// Said once. A region that has stopped being readable is not a layout problem and not
// something the player can act on, so the line exists to stop a caught fault reading as
// silence rather than to be acted upon.
void ReportFaultOnce(const void* p, std::size_t bytes) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    Log::Line("[ue3] a %u byte read of the game's memory at 0x%p faulted and was refused. "
              "The region test in front of every read answers for the instant it runs, "
              "and the game frees memory on its own threads while this walk is in "
              "progress, so a region seen committed can be gone by the time a word inside "
              "it is read. The walk carries on and nothing is written to the game.",
              static_cast<unsigned>(bytes), p);
}

int FaultFilter(unsigned long code) {
    // An access violation or a failed page-in is the region going away underneath the
    // read. Anything else is this mod's own bug and belongs in the crash it would
    // otherwise be hidden behind.
    if (code != static_cast<unsigned long>(EXCEPTION_ACCESS_VIOLATION) &&
        code != static_cast<unsigned long>(EXCEPTION_IN_PAGE_ERROR)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// By value, not by reference into the table: the caller goes on to compare bounds, and a
// reference would have it comparing against whatever the table held by then.
Region RegionOf(const void* p) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    for (int i = 0; i < g_regionCount; ++i) {
        if (b < g_regions[i].start || b >= g_regions[i].end) {
            continue;
        }
        const Region hit = g_regions[i];
        // To the front. The walk asks about the same region in bursts - a whole object's
        // window, then a whole name - so the next question is nearly always this one.
        for (int j = i; j > 0; --j) {
            g_regions[j] = g_regions[j - 1];
        }
        g_regions[0] = hit;
        return hit;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) {
        return Region{};
    }
    const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY;
    const DWORD writable = PAGE_READWRITE | PAGE_EXECUTE_READWRITE;
    Region r;
    r.start = static_cast<const std::uint8_t*>(mbi.BaseAddress);
    r.end = r.start + mbi.RegionSize;
    r.readable = mbi.State == MEM_COMMIT && (mbi.Protect & readable) != 0 &&
                 (mbi.Protect & PAGE_GUARD) == 0;
    r.writable = r.readable && (mbi.Protect & writable) != 0;
    // Only a COMMITTED region is remembered. A free or reserved one turns committed as
    // the game grows its heap, and nothing would ever evict the stale "no" - a caught
    // fault is what drops the table, and a read inside a region already cached as
    // unreadable never runs, so it can never fault its way out. On a 32-bit heap those
    // ranges are tens of megabytes wide, so one cached negative would silently skip every
    // object the game later allocates inside it, and the walk would report reaching
    // thousands of objects while never seeing the one it was sent for.
    if (mbi.State != MEM_COMMIT) {
        return r;
    }
    if (g_regionCount < kRegionCacheEntries) {
        ++g_regionCount;
    }
    for (int j = g_regionCount - 1; j > 0; --j) {
        g_regions[j] = g_regions[j - 1];
    }
    g_regions[0] = r;
    return r;
}

// A word that could be a pointer into this process's user address space. Only ever a
// filter in front of IsObject - nothing is dereferenced on its say-so.
bool PlausiblePointer(std::uint32_t v) {
    return v >= 0x10000u && v < 0x80000000u && (v & 3u) == 0u;
}

// Zero on a refused read, which every caller already treats as the answer it is: a null
// class pointer is not an object, a null name index is out of the pool, a zero word is
// not a plausible pointer.
template <typename T>
T Read(const void* p, std::size_t offset) {
    T value{};
    CopyBytes(&value, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return value;
}

const void* ReadPtr(const void* p, std::size_t offset) {
    return Read<const void*>(p, offset);
}

// The name pool: a { Data, Num, Max } triple in the exe's own data, empty until the
// engine's static initialisers have run.
bool Names(const std::uint8_t* const** dataOut, std::int32_t* numOut) {
    const std::uintptr_t base = ExeBase();
    if (!base) {
        return false;
    }
    const std::uint8_t* const* data =
        *reinterpret_cast<const std::uint8_t* const* const*>(base + L().rvaGNamesData);
    const std::int32_t num =
        *reinterpret_cast<const std::int32_t*>(base + L().rvaGNamesNum);
    if (!data || num <= 0 || !Readable(data, sizeof(void*))) {
        return false;
    }
    if (dataOut) *dataOut = data;
    if (numOut) *numOut = num;
    return true;
}

char Printable(std::uint32_t c) {
    return (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?';
}

}  // namespace

// Every read of the game's memory goes through here. VirtualQuery cannot make one safe:
// it describes the address space at the instant it is called, and the walk below reads
// millions of words after it, from a heap the game's other threads are still freeing. So
// the region test is the cheap filter that keeps this off obvious nonsense and this is
// the part that decides the answer - a read that faults is a read that failed, not a
// crash in the player's game.
bool CopyBytes(void* dst, const void* src, std::size_t bytes) {
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    } __except (FaultFilter(GetExceptionCode())) {
        // Some cached region just proved itself stale, and which one is not worth
        // working out. Dropping the lot makes the next reads ask the kernel again, so a
        // freed region costs a handful of caught faults rather than one per word for as
        // long as the walk stays inside it.
        ForgetRegions();
        ReportFaultOnce(src, bytes);
        return false;
    }
}

// The mirror of CopyBytes for the only thing this mod writes into the game - the
// crosshair's two visibility bits. A refused write is not the same answer as a refused
// read (nothing has changed either way), but it must not be the access violation that
// takes the player's game down: the widget can be collected between the check and the
// store, and that window is a crash rather than a caught fault without this.
bool WriteBytes(void* dst, const void* src, std::size_t bytes) {
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    } __except (FaultFilter(GetExceptionCode())) {
        ForgetRegions();
        ReportFaultOnce(dst, bytes);
        return false;
    }
}

bool Readable(const void* p, std::size_t bytes) {
    if (!p || bytes == 0) {
        return false;
    }
    const Region r = RegionOf(p);
    if (!r.readable) {
        return false;
    }
    const auto* b = static_cast<const std::uint8_t*>(p);
    return b >= r.start && b < r.end && bytes <= static_cast<std::size_t>(r.end - b);
}

bool Writable(const void* p, std::size_t bytes) {
    if (!p || bytes == 0) {
        return false;
    }
    // Asked of the kernel every time, never of the cache.
    //
    // A cached entry describes the address space as it was when the region was first seen,
    // and nothing invalidates a stale POSITIVE except a fault. For a read that is
    // harmless - the guard catches it. For a write it is not: if the cached bounds now
    // span a different live allocation, the store lands, silently, on memory this mod does
    // not own. Writes happen a handful of times per frame, so the query is affordable
    // where it is not on the walk's hot path.
    ForgetRegions();
    const Region r = RegionOf(p);
    // Deliberately narrower than Readable: that one accepts PAGE_READONLY and
    // PAGE_WRITECOPY, and approving a store to either is a fault with no handler in front
    // of it. A write has to ask whether the page takes writes, not whether it gives reads.
    if (!r.writable) {
        return false;
    }
    const auto* b = static_cast<const std::uint8_t*>(p);
    return b >= r.start && b < r.end && bytes <= static_cast<std::size_t>(r.end - b);
}

// The two accessors the crosshair uses. The offset arrives from the engine's own property
// record, so it is widened before the bound is computed: `offset + 4` in 32-bit unsigned
// arithmetic wraps to a small number for an offset near the top of the range, and the
// bounds test would then pass for an address BEFORE the object - a silent four-byte
// corruption of somebody else's heap rather than a refused read.
bool ReadDwordAt(const void* obj, std::uint32_t byteOffset, std::uint32_t* out) {
    const std::uint64_t span =
        static_cast<std::uint64_t>(byteOffset) + sizeof(std::uint32_t);
    if (!obj || !out || span > kMaxFieldSpan || !Readable(obj, static_cast<std::size_t>(span))) {
        return false;
    }
    return CopyBytes(out, static_cast<const std::uint8_t*>(obj) + byteOffset,
                     sizeof(std::uint32_t));
}

bool WriteDwordAt(void* obj, std::uint32_t byteOffset, std::uint32_t value) {
    const std::uint64_t span =
        static_cast<std::uint64_t>(byteOffset) + sizeof(std::uint32_t);
    if (!obj || span > kMaxFieldSpan || !Writable(obj, static_cast<std::size_t>(span))) {
        return false;
    }
    return WriteBytes(static_cast<std::uint8_t*>(obj) + byteOffset, &value,
                      sizeof(std::uint32_t));
}

std::size_t ReadableSpan(const void* p, std::size_t cap) {
    if (!p) {
        return 0;
    }
    const Region r = RegionOf(p);
    if (!r.readable) {
        return 0;
    }
    const auto* b = static_cast<const std::uint8_t*>(p);
    if (b < r.start || b >= r.end) {
        return 0;
    }
    const std::size_t available = static_cast<std::size_t>(r.end - b);
    return available < cap ? available : cap;
}

bool NamesReady() {
    return Names(nullptr, nullptr);
}

bool NameText(std::int32_t index, char* out, std::size_t outSize) {
    if (!out || outSize < 2) {
        return false;
    }
    out[0] = '\0';
    const std::uint8_t* const* data = nullptr;
    std::int32_t num = 0;
    if (!Names(&data, &num) || index < 0 || index >= num) {
        return false;
    }
    if (!Readable(data + index, sizeof(void*))) {
        return false;
    }
    const auto* entry = static_cast<const std::uint8_t*>(ReadPtr(data + index, 0));
    if (!Readable(entry, L().offNameEntryText + 2)) {
        return false;
    }
    // The entry records its own index, so a slot that disagrees with the index it was
    // reached by is either recycled memory or a layout this profile no longer describes.
    // Either way it is not the name that was asked for.
    const std::uint32_t packed = Read<std::uint32_t>(entry, L().offNameEntryPacked);
    if (static_cast<std::int32_t>(packed >> 1) != index) {
        return false;
    }
    const bool wide = (packed & 1u) != 0;
    const std::uint8_t* text = entry + L().offNameEntryText;
    const std::size_t charBytes = wide ? 2u : 1u;
    for (std::size_t n = 0; n + 1 < outSize; ++n) {
        const std::uint8_t* c = text + n * charBytes;
        std::uint8_t raw[2] = {};
        // A refused read is not an empty string: taking it as one would end the name
        // early and hand back a prefix that compares equal to the wrong thing, which is
        // the one failure this whole function is shaped to avoid.
        if (!Readable(c, charBytes) || !CopyBytes(raw, c, charBytes)) {
            out[0] = '\0';
            return false;
        }
        const std::uint32_t ch =
            wide ? static_cast<std::uint32_t>(raw[0] | (raw[1] << 8)) : raw[0];
        if (ch == 0) {
            out[n] = '\0';
            return true;
        }
        out[n] = Printable(ch);
    }
    out[0] = '\0';
    return false;
}

bool IsObject(const void* p) {
    const std::size_t need = L().offObjectClass + sizeof(void*);
    if (!p || (reinterpret_cast<std::uintptr_t>(p) & 3u) != 0 || !Readable(p, need)) {
        return false;
    }
    const void* cls = ReadPtr(p, L().offObjectClass);
    if (!cls || !Readable(cls, need)) {
        return false;
    }
    // Every class is itself an object whose class is the class-of-classes, and that one
    // is its own class. Two hops and a fixpoint: a plain struct carrying a stray pointer
    // does not survive it.
    const void* meta = ReadPtr(cls, L().offObjectClass);
    if (!meta || !Readable(meta, need) || ReadPtr(meta, L().offObjectClass) != meta) {
        return false;
    }
    std::int32_t num = 0;
    if (!Names(nullptr, &num)) {
        return false;
    }
    const std::int32_t nameIndex = Read<std::int32_t>(p, L().offObjectName);
    return nameIndex >= 0 && nameIndex < num;
}

bool ObjectName(const void* obj, char* out, std::size_t outSize) {
    if (!IsObject(obj)) {
        return false;
    }
    return NameText(Read<std::int32_t>(obj, L().offObjectName), out, outSize);
}

bool ClassName(const void* obj, char* out, std::size_t outSize) {
    if (!IsObject(obj)) {
        return false;
    }
    return ObjectName(ReadPtr(obj, L().offObjectClass), out, outSize);
}

const void* Outer(const void* obj) {
    if (!IsObject(obj)) {
        return nullptr;
    }
    const void* outer = ReadPtr(obj, L().offObjectOuter);
    return IsObject(outer) ? outer : nullptr;
}

namespace {

// The named property on the object's class chain, and its type. Up the chain, and along
// each class's own field list: a property declared on a base class is as much this
// object's as one declared on its own class, which is why the walk does not stop at the
// first level.
const void* FindProperty(const void* obj, const char* propertyName, char* kindOut,
                         std::size_t kindSize) {
    const std::size_t structNeed = L().offStructChildren + sizeof(void*);
    const void* cls = ReadPtr(obj, L().offObjectClass);
    for (int level = 0; level < 24 && cls; ++level) {
        if (!Readable(cls, structNeed)) {
            return nullptr;
        }
        const void* field = ReadPtr(cls, L().offStructChildren);
        for (int i = 0; i < 4096 && field; ++i) {
            if (!IsObject(field) || !Readable(field, L().offFieldNext + sizeof(void*))) {
                break;
            }
            char name[128];
            if (ObjectName(field, name, sizeof(name)) &&
                std::strcmp(name, propertyName) == 0) {
                return ClassName(field, kindOut, kindSize) ? field : nullptr;
            }
            field = ReadPtr(field, L().offFieldNext);
        }
        cls = ReadPtr(cls, L().offStructSuper);
    }
    return nullptr;
}

}  // namespace

bool FindBoolField(const void* obj, const char* propertyName, BoolField* out) {
    if (!IsObject(obj) || !propertyName || !out) {
        return false;
    }
    char kind[64] = {};
    const void* field = FindProperty(obj, propertyName, kind, sizeof(kind));
    // The name may be on the chain and not be a bool. Refusing then is the whole point of
    // looking it up by name: writing a bit through an offset that belongs to something
    // else is what this avoids.
    if (!field || std::strcmp(kind, "BoolProperty") != 0 ||
        !Readable(field, L().offBoolPropertyMask + sizeof(std::uint32_t))) {
        return false;
    }
    out->byte_offset = Read<std::uint32_t>(field, L().offPropertyOffset);
    out->mask = Read<std::uint32_t>(field, L().offBoolPropertyMask);
    return out->mask != 0;
}

const void* FindObjectField(const void* obj, const char* propertyName) {
    if (!IsObject(obj) || !propertyName) {
        return nullptr;
    }
    char kind[64] = {};
    const void* field = FindProperty(obj, propertyName, kind, sizeof(kind));
    if (!field || std::strcmp(kind, "ObjectProperty") != 0 ||
        !Readable(field, L().offPropertyOffset + sizeof(std::uint32_t))) {
        return nullptr;
    }
    const std::uint32_t offset = Read<std::uint32_t>(field, L().offPropertyOffset);
    const std::uint64_t span = static_cast<std::uint64_t>(offset) + sizeof(void*);
    if (span > kMaxFieldSpan || !Readable(obj, static_cast<std::size_t>(span))) {
        return nullptr;
    }
    const void* value = ReadPtr(obj, offset);
    return IsObject(value) ? value : nullptr;
}

namespace {

// The walk's budgets. Any one of them alone ends the search, so a graph shaped in a way
// none of them anticipated still costs a bounded fraction of one frame rather than a
// hang. They are sized to reach the screen widgets from the player controller with room
// to spare, not to be reached.
constexpr int kMaxVisited = 24576;
constexpr int kMaxDepth = 6;
constexpr std::size_t kObjectWindow = 0x1000;
constexpr std::size_t kMaxWordsRead = 2u * 1000u * 1000u;
// An array header is only followed when its count could be a real one.
constexpr std::int32_t kMaxArrayElements = 4096;

// Open addressed, power of two, and never emptied entry by entry - the whole table is
// cleared once per search. Linear scanning the visited list instead would be sixteen
// million comparisons on a full queue, which is the difference between a hitch and a
// stall.
constexpr int kSeenSlots = 65536;

struct Walk {
    const void* queue[kMaxVisited];
    int depth[kMaxVisited];
    const void* seen[kSeenSlots];
    int count;
    std::size_t words;

    void Reset() {
        count = 0;
        words = 0;
        std::memset(seen, 0, sizeof(seen));
    }

    bool Push(const void* p, int d) {
        std::size_t slot = (reinterpret_cast<std::uintptr_t>(p) >> 2) & (kSeenSlots - 1);
        for (int probe = 0; probe < kSeenSlots; ++probe) {
            if (seen[slot] == p) {
                return true;
            }
            if (!seen[slot]) {
                break;
            }
            slot = (slot + 1) & (kSeenSlots - 1);
        }
        if (count >= kMaxVisited) {
            return false;
        }
        seen[slot] = p;
        queue[count] = p;
        depth[count] = d;
        ++count;
        return true;
    }
};

// Several kilobytes of table, allocated once for the life of the process rather than on
// the stack of a frame: the game thread's stack has no room for it.
Walk g_walk;
int g_visited = 0;

}  // namespace

int FindInstances(const void* root, const char* className, void** out, int cap) {
    g_visited = 0;
    if (!className || !out || cap <= 0 || !NamesReady() || !IsObject(root)) {
        return -1;
    }

    // Staleness is bounded to one walk. Every cached answer describes the address space
    // as it was during the previous search, which on a level change is a different heap
    // entirely, and a region the game has since freed and re-committed would otherwise be
    // answered from the old entry for the rest of the session.
    ForgetRegions();
    g_walk.Reset();
    g_walk.Push(root, 0);

    int found = 0;
    for (int head = 0; head < g_walk.count; ++head) {
        const void* obj = g_walk.queue[head];
        const int depth = g_walk.depth[head];
        ++g_visited;

        char cls[128];
        if (ClassName(obj, cls, sizeof(cls)) && std::strcmp(cls, className) == 0) {
            out[found++] = const_cast<void*>(obj);
            if (found >= cap) {
                break;
            }
        }
        if (depth >= kMaxDepth || g_walk.words >= kMaxWordsRead) {
            continue;
        }

        const std::size_t span = ReadableSpan(obj, kObjectWindow);
        // Past the class slot: everything before it is the object header, and the one
        // pointer in it worth following (the outer) leads up into packages rather than
        // down towards the screen the caller is after.
        const std::size_t first = L().offObjectClass + 2 * sizeof(void*);
        for (std::size_t off = first; off + 4 <= span; off += 4) {
            ++g_walk.words;
            const std::uint32_t v = Read<std::uint32_t>(obj, off);
            if (!PlausiblePointer(v)) {
                continue;
            }
            const void* p = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(v));
            if (IsObject(p)) {
                if (!g_walk.Push(p, depth + 1)) {
                    break;
                }
                continue;
            }
            // A dynamic array of objects is a { Data, Num, Max } triple, so the objects
            // in it are not pointers in the owner at all. Widgets sit in exactly such an
            // array on the screen that owns them, so a walk that followed only plain
            // pointers would stop one step short of every one of them.
            if (off + 12 > span) {
                continue;
            }
            const std::int32_t num = Read<std::int32_t>(obj, off + 4);
            const std::int32_t max = Read<std::int32_t>(obj, off + 8);
            if (num <= 0 || num > kMaxArrayElements || max < num ||
                !Readable(p, static_cast<std::size_t>(num) * 4)) {
                continue;
            }
            for (std::int32_t i = 0; i < num; ++i) {
                ++g_walk.words;
                const std::uint32_t e =
                    Read<std::uint32_t>(p, static_cast<std::size_t>(i) * 4);
                if (!PlausiblePointer(e)) {
                    continue;
                }
                const void* ep =
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(e));
                if (IsObject(ep) && !g_walk.Push(ep, depth + 1)) {
                    break;
                }
            }
        }
    }
    return found;
}

int LastSearchVisited() {
    return g_visited;
}

namespace {

// What one property is worth writing next to its name. Anything else is named and left
// unread: a struct or an array printed as a number is worse than nothing, because it
// reads as a measurement.
void AppendValue(const void* obj, const char* kind, std::uint32_t offset,
                 std::uint32_t mask, char* out, std::size_t outSize) {
    out[0] = '\0';
    const std::uint64_t span = static_cast<std::uint64_t>(offset) + 8;
    if (span > kMaxFieldSpan || !Readable(obj, static_cast<std::size_t>(span))) {
        return;
    }
    if (std::strcmp(kind, "FloatProperty") == 0) {
        _snprintf_s(out, outSize, _TRUNCATE, " = %.4f", Read<float>(obj, offset));
    } else if (std::strcmp(kind, "IntProperty") == 0) {
        _snprintf_s(out, outSize, _TRUNCATE, " = %d", Read<std::int32_t>(obj, offset));
    } else if (std::strcmp(kind, "ByteProperty") == 0) {
        _snprintf_s(out, outSize, _TRUNCATE, " = %u", Read<std::uint8_t>(obj, offset));
    } else if (std::strcmp(kind, "BoolProperty") == 0) {
        _snprintf_s(out, outSize, _TRUNCATE, " = %d (mask 0x%X)",
                    (Read<std::uint32_t>(obj, offset) & mask) != 0 ? 1 : 0, mask);
    } else if (std::strcmp(kind, "NameProperty") == 0) {
        char text[128] = {};
        NameText(Read<std::int32_t>(obj, offset), text, sizeof(text));
        _snprintf_s(out, outSize, _TRUNCATE, " = '%s'", text);
    } else if (std::strcmp(kind, "ObjectProperty") == 0) {
        char cls[128] = {};
        char name[128] = {};
        const void* p = ReadPtr(obj, offset);
        if (p && ClassName(p, cls, sizeof(cls))) {
            ObjectName(p, name, sizeof(name));
            _snprintf_s(out, outSize, _TRUNCATE, " = %s '%s'", cls, name);
        } else {
            _snprintf_s(out, outSize, _TRUNCATE, " = %s", p ? "<not an object>" : "none");
        }
    }
}

// A cap on one dump, so a class chain longer than anything measured still ends.
constexpr int kMaxDumpedProperties = 900;

}  // namespace

void DumpProperties(const void* obj, const char* label) {
    char cls[128] = {};
    if (!ClassName(obj, cls, sizeof(cls))) {
        Log::Line("[probe] %s at 0x%p does not read as an engine object", label, obj);
        return;
    }
    Log::Line("[probe] %s: %s at 0x%p", label, cls, obj);

    const std::size_t structNeed = L().offStructChildren + sizeof(void*);
    const void* walk = ReadPtr(obj, L().offObjectClass);
    int dumped = 0;
    for (int level = 0; level < 24 && walk && dumped < kMaxDumpedProperties; ++level) {
        if (!Readable(walk, structNeed)) {
            return;
        }
        char owner[128] = {};
        ObjectName(walk, owner, sizeof(owner));
        const void* field = ReadPtr(walk, L().offStructChildren);
        for (int i = 0; i < 4096 && field && dumped < kMaxDumpedProperties; ++i) {
            if (!IsObject(field) || !Readable(field, L().offBoolPropertyMask + 4)) {
                break;
            }
            char kind[64] = {};
            char name[128] = {};
            ClassName(field, kind, sizeof(kind));
            ObjectName(field, name, sizeof(name));
            const std::uint32_t offset = Read<std::uint32_t>(field, L().offPropertyOffset);
            const std::uint32_t mask = Read<std::uint32_t>(field, L().offBoolPropertyMask);
            char value[192];
            AppendValue(obj, kind, offset, mask, value, sizeof(value));
            Log::Line("[probe]  [%s] %-20s %-34s +0x%03X%s", owner, kind, name, offset,
                      value);
            ++dumped;
            field = ReadPtr(field, L().offFieldNext);
        }
        walk = ReadPtr(walk, L().offStructSuper);
    }
    Log::Line("[probe] %s: %d properties listed", label, dumped);
}

void ReportOnce(const void* playerController) {
    static bool reported = false;
    if (reported) {
        return;
    }
    reported = true;
    std::int32_t num = 0;
    Names(nullptr, &num);
    char cls[128] = {};
    const bool ok = ClassName(playerController, cls, sizeof(cls));
    Log::Line("[ue3] name pool holds %d names; the player controller at 0x%p reads as "
              "class '%s'%s",
              num, playerController, ok ? cls : "<not an object>",
              ok ? ""
                 : " - the object layout in this build profile does not fit this EXE, so "
                   "the game's own crosshair will be left alone");
}

}  // namespace ue3
}  // namespace BioShockInfiniteHeadTracking
