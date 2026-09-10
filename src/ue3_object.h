#pragma once

#include <cstddef>
#include <cstdint>

namespace BioShockInfiniteHeadTracking {
namespace ue3 {

// The engine's own object model, read from outside it.
//
// Every dereference is gated: a pointer is only followed once it has passed IsObject,
// every access is bounded by the memory region it lands in, and the access itself is
// guarded - the region test describes the address space at the instant it runs, and the
// game frees memory on its own threads while a walk is in progress, so an access that
// faults is answered as one that failed.
//
// All of it reads except WriteDwordAt, which exists for the one thing the mod changes in
// the game: the crosshair widget's visibility bits. That one goes through the same gate
// with a stricter page test, because a store to a read-only page faults where a read
// would not.
// Nothing is derived from a guess - the byte offsets come from the matched build profile,
// and the one thing that could silently go wrong on a build whose layout moved (reading
// something that is not a name and believing it) is caught by ReportOnce rather than by a
// wrong write.
//
// Game thread only. The object graph is the game's, and it is rearranged by garbage
// collection and by level loads, so nothing found here may be held across either - which
// is why the callers re-run the search rather than keeping a pointer.

// Whether `bytes` from `p` are committed and readable. The answer is cached per memory
// region, so a walk that reads one object at a time pays for one query per object rather
// than one per word.
bool Readable(const void* p, std::size_t bytes);

// A guarded copy out of the game's memory. The region test in front of every read
// describes the address space at the instant it runs, and the game frees memory on its
// own threads, so a region seen committed can be gone by the time a word inside it is
// read. False means the read faulted and was refused; the destination is untouched.
bool CopyBytes(void* dst, const void* src, std::size_t bytes);

// Whether `bytes` from `p` accept a STORE. Narrower than Readable, which is satisfied by
// a read-only or copy-on-write page: approving a write to one of those is an access
// violation with nothing in front of it.
bool Writable(const void* p, std::size_t bytes);

// One dword of an object, addressed by the byte offset the engine's own property record
// gave. Both refuse an offset too large to be a field, and both bound the object before
// touching it; the write additionally requires the page to be writable and is guarded, so
// a widget collected between the check and the store is a refused write rather than a
// crash in the player's game.
bool ReadDwordAt(const void* obj, std::uint32_t byteOffset, std::uint32_t* out);
bool WriteDwordAt(void* obj, std::uint32_t byteOffset, std::uint32_t value);

// How much of `p` is readable, up to `cap`. What the object walk uses in place of an
// object size, which the engine does not record anywhere this side can reach.
std::size_t ReadableSpan(const void* p, std::size_t cap);

// True once the engine's name pool has been populated. It is empty until the exe's own
// static initialisers have run, so nothing below may be called from DllMain.
bool NamesReady();

// The name pool entry's text, as printable ASCII. False when the index is outside the
// pool, when the entry's own recorded index disagrees with the one asked for (a recycled
// slot, or a layout this profile no longer describes), or when no terminator sits inside
// the buffer - never a truncated name, which would compare equal to the wrong thing.
bool NameText(std::int32_t index, char* out, std::size_t outSize);

// Whether a pointer reads as a genuine engine object. The test is the class fixpoint:
// every object's class is an object, and the class of a class's class is itself. A plain
// struct that happens to hold a stray pointer does not survive it.
bool IsObject(const void* p);

// The object's own name, and the name of its class. Both false on anything IsObject
// rejects.
bool ObjectName(const void* obj, char* out, std::size_t outSize);
bool ClassName(const void* obj, char* out, std::size_t outSize);

// The object this one belongs to. For a screen widget that is the screen holding it,
// which is where anything the widget is driven WITH lives.
const void* Outer(const void* obj);

// A bool field's home: which byte of the object holds it and which bit of that dword it
// is. Several bools share one dword, so the mask is as much a part of the answer as the
// offset, and both are read from the property the engine itself uses.
struct BoolField {
    std::uint32_t byte_offset = 0;
    std::uint32_t mask = 0;
};

// Finds a named bool property on the object's class chain. False when the chain carries
// no such property, or carries one that is not a bool - which is the refusal that
// matters: a caller that cannot find the field must leave the object alone rather than
// write through an offset it guessed.
bool FindBoolField(const void* obj, const char* propertyName, BoolField* out);

// The object held by a named object property, or null when the class chain carries no
// such property, it is not an object property, or it is empty. What turns a walk of the
// whole world into a walk of one screen: start from the thing that owns what you want.
const void* FindObjectField(const void* obj, const char* propertyName);

// Objects of a named class reachable from `root` by following object pointers, breadth
// first. Returns the number written to `out`, or -1 when the search could not run at all
// (the name pool is empty, or the root is not an object).
//
// This is how a live widget is reached without sweeping the heap: the engine's collector
// keeps a widget alive by a reference from something else, so a widget that is on screen
// is reachable from the player controller by definition. The walk is bounded on depth,
// on objects visited and on words read, so its cost is a known one-frame hitch rather
// than an open-ended search.
int FindInstances(const void* root, const char* className, void** out, int cap);

// How many objects the last FindInstances visited. For the log line that says a search
// ran and came up empty, which is a different problem from a search that never ran.
int LastSearchVisited();

// Every property on the object's class chain, with its type, its byte offset and its live
// value. Several hundred lines for one object, so it is only ever reached from
// [Diagnostics] StateProbe.
//
// This is the instrument the remaining reticle work needs. The mark is placed along the
// clean aim DIRECTION, which leaves an error of lean over distance under a positional
// lean, and closing that needs the distance to whatever the aim is on. The game computes
// one already - it sizes the crosshair's spread as a screen distance, and its own
// stereoscopic reticle sits at a depth - so the field exists on one of these objects and
// this is what names it. Reading a number the game maintains beats casting a ray of our
// own: no engine call, no collision filter to get wrong, and it is by construction the
// distance the game's own crosshair is drawn for.
void DumpProperties(const void* obj, const char* label);

// One line naming what the object model resolved to, and whether the player controller
// handed in reads back as one. Called once, from the first search: a layout that had
// moved would otherwise show up as a widget that is never found, with nothing in the log
// to say the reason was the layout rather than the widget.
void ReportOnce(const void* playerController);

}  // namespace ue3
}  // namespace BioShockInfiniteHeadTracking
