#pragma once

#include <cctype>
#include <cstring>
#include <initializer_list>

namespace cameraunlock::ads {

// What head tracking does while the sights are up.
//
// Every mod in the fleet that covers a game with an aim-down-sights state ships
// the same cycle, the same value strings, the same toast wording and the same
// key, so a player who learns this in one shooter knows it in all of them. That
// is only true if there is exactly one copy of the strings, which is this file:
// a mod that spells its own "tracking on, aim marker shown" has already drifted.
//
// The config key is always `ads_mode` (spelled `AdsMode` in an INI), it always
// defaults to `Paused`, and the settings panel dropdown - where a mod has one -
// carries the same order as the cycle below so the key and the panel walk the
// modes the same way.
enum class AdsMode {
    // Tracking stands down for as long as the sights are up. Stock ADS,
    // indistinguishable from an unmodded game.
    Paused,
    // Tracking stays live through the aim, and the mod draws its own marker at
    // the projected impact point.
    Marker,
    // Tracking stays live, nothing drawn.
    Tracked,
};

// The default is the mode that cannot be wrong: raising the sights removes the
// head rotation from the view, which by itself swings the view onto the point
// the reticle was marking, and from there the game owns the sight picture.
constexpr AdsMode kDefaultAdsMode = AdsMode::Paused;

// ---- Two slots or three ------------------------------------------------------
//
// A game whose own ADS reticle is an aim indicator at the impact point AND is
// reachable by the mod's reticle compensation while the sights are up ships TWO
// slots - `Paused` and `Tracked` - and never mentions `Marker` anywhere: not in
// its config validation, not in its panel, not in its README. Everything else
// ships three. A scope's own reticle does not count, because it is only honest
// while the eye sits exactly on the optic, which is precisely what head tracking
// breaks. This header carries all three names; which of them a given mod offers
// is that mod's cycle table, not this enum.

// The strings a config file holds. Cross-mod contract: never localised, never
// renamed to match a game's own wording.
inline const char* AdsModeValue(AdsMode mode) {
    switch (mode) {
        case AdsMode::Paused:  return "paused";
        case AdsMode::Marker:  return "marker";
        case AdsMode::Tracked: return "tracked";
    }
    return "paused";
}

// What the key press says it did.
inline const char* AdsModeToast(AdsMode mode) {
    switch (mode) {
        case AdsMode::Paused:  return "ADS: tracking paused";
        case AdsMode::Marker:  return "ADS: tracking on, aim marker shown";
        case AdsMode::Tracked: return "ADS: tracking on, no aim marker";
    }
    return "ADS: tracking paused";
}

// The label a settings panel shows against each value.
inline const char* AdsModeLabel(AdsMode mode) {
    switch (mode) {
        case AdsMode::Paused:  return "Tracking paused";
        case AdsMode::Marker:  return "Tracking on, aim marker shown";
        case AdsMode::Tracked: return "Tracking on, no aim marker";
    }
    return "Tracking paused";
}

// Three-slot cycle. A two-slot mod uses NextAdsModeTwoSlot below rather than
// shipping a third slot that only cycles back to the first.
inline AdsMode NextAdsMode(AdsMode mode) {
    switch (mode) {
        case AdsMode::Paused:  return AdsMode::Marker;
        case AdsMode::Marker:  return AdsMode::Tracked;
        case AdsMode::Tracked: return AdsMode::Paused;
    }
    return kDefaultAdsMode;
}

// Two-slot cycle, for a game whose own ADS reticle the mod can drive.
inline AdsMode NextAdsModeTwoSlot(AdsMode mode) {
    return mode == AdsMode::Paused ? AdsMode::Tracked : AdsMode::Paused;
}

// True when this mode stands tracking down for the duration of the aim, which is
// the one branch that closes a mod's tracking gate on the sights coming up.
// Named rather than spelled `mode == AdsMode::Paused` in every mod, so the rule
// has a single definition on both sides of the library.
inline bool AdsSuspendsTracking(AdsMode mode) { return mode == AdsMode::Paused; }

// Anything that is not one of the three values is the DEFAULT, not whichever
// branch happens to be last. That covers a typo in a hand-edited file, and it is
// also the migration path for a mode renamed since an older release wrote the
// setting: the player lands on stock ADS rather than on head tracking through
// their sights that they never asked for.
//
// `allowMarker` is false for a two-slot mod, where the string `marker` must not
// resolve to anything - a config written by a three-slot sibling, or by an older
// release of the same mod, otherwise selects a mode that does not exist.
inline AdsMode ParseAdsMode(const char* text, bool allowMarker = true) {
    if (!text) return kDefaultAdsMode;
    // The same set at both ends, and the same set the C# half trims. It used to
    // be " \t" leading and " \t\r\n" trailing here, against string.Trim() over
    // there, so one file could give a C# mod `tracked` and a C++ mod `paused`.
    // ASCII only on purpose: a value carrying a non-breaking space is a file to
    // fail toward the default on, not one to guess at.
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    };
    while (isSpace(*text)) ++text;
    size_t len = std::strlen(text);
    while (len > 0 && isSpace(text[len - 1])) --len;
    for (const AdsMode mode : { AdsMode::Paused, AdsMode::Marker, AdsMode::Tracked }) {
        if (mode == AdsMode::Marker && !allowMarker) continue;
        const char* value = AdsModeValue(mode);
        if (std::strlen(value) != len) continue;
        size_t i = 0;
        for (; i < len; ++i) {
            const int a = std::tolower(static_cast<unsigned char>(text[i]));
            const int b = static_cast<unsigned char>(value[i]);
            if (a != b) break;
        }
        if (i == len) return mode;
    }
    return kDefaultAdsMode;
}

}  // namespace cameraunlock::ads
