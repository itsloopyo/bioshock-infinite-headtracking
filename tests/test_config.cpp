// Characterization tests for the INI: what the shipped file contains, what the reader
// makes of a hand-edited one, and what survives an in-game ADS cycle writing one key back.
//
// The config is the one part of the mod a player edits, so a change of meaning here
// reaches them as a setting that quietly stopped working. The three things pinned below
// are the ones with somewhere to go wrong: the defaults are stated in three places
// (config.h, the writer, the reader's fallbacks) and must agree; the boundary checks turn
// a typo into the shipped value rather than into a NaN in the view matrix; and
// SaveAdsMode has to leave every other key and comment in the file alone.
//
// Windows-only, like test_port_reclaim.cpp: the single-key writer is
// WritePrivateProfileStringA. Until this repo has a build system, run it from the repo
// root with
/*
   g++ -std=c++17 -Isrc -Icameraunlock-core/cpp/include tests/test_config.cpp \
       src/config.cpp cameraunlock-core/cpp/src/config/ini_reader.cpp \
       cameraunlock-core/cpp/src/logging/file_log.cpp -o config_tests.exe
*/
// then run config_tests.exe. Exit code 0 means every check passed.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s\n", __FILE__, line, what);
}

void CheckNear(float got, float want, const char* what, int line) {
    ++g_checks;
    const float diff = got - want;
    if (diff <= 1e-4f && diff >= -1e-4f) return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s: got %.6f, want %.6f\n", __FILE__, line, what, got, want);
}

#define CHECK(cond) Check((cond), #cond, __LINE__)
#define CHECK_NEAR(got, want) CheckNear((got), (want), #got, __LINE__)

using namespace BioShockInfiniteHeadTracking;

// GetPrivateProfileString resolves a bare filename against the WINDOWS directory rather
// than the working one, which is why the mod builds an absolute path of its own
// (path_utils.h) and why the tests have to drive it through one too. A relative path here
// reads someone else's file, and every check below then passes on the defaults.
std::string AbsolutePath(const char* name) {
    char buf[MAX_PATH];
    const DWORD written = GetFullPathNameA(name, MAX_PATH, buf, nullptr);
    if (written == 0 || written >= MAX_PATH) {
        std::printf("could not resolve %s to an absolute path\n", name);
        std::exit(2);
    }
    return buf;
}

const char* IniPath() {
    static const std::string path = AbsolutePath("test_config_tmp.ini");
    return path.c_str();
}

void WriteIni(const char* body) {
    std::remove(IniPath());
    std::FILE* f = std::fopen(IniPath(), "wb");
    if (!f) {
        std::printf("could not write %s\n", IniPath());
        ++g_failures;
        return;
    }
    std::fwrite(body, 1, std::strlen(body), f);
    std::fclose(f);
}

std::string ReadIni() {
    std::FILE* f = std::fopen(IniPath(), "rb");
    if (!f) return {};
    std::string out;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

// The file the mod writes on first launch must read back as the defaults it was written
// from. The three statements of each default - config.h, the writer, the reader's
// fallback - have no other check that they agree.
void TestFreshInstallRoundTripsTheShippedDefaults() {
    std::remove(IniPath());
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));

    CHECK(c.enabled_on_startup == defaults::kEnableOnStartup);
    CHECK(c.udp_port == defaults::kPort);
    CHECK(c.data_freshness_ms == defaults::kDataFreshnessMs);
    CHECK(c.world_space_yaw == defaults::kWorldSpaceYaw);
    CHECK(c.show_aim_marker == defaults::kShowAimMarker);
    CHECK(c.ads_mode == defaults::kAdsMode);
    CHECK_NEAR(c.fov_override, defaults::kFovOverride);
    CHECK_NEAR(c.local_smoothing, defaults::kLocalSmoothing);
    CHECK_NEAR(c.remote_smoothing, defaults::kRemoteSmoothing);
    CHECK(c.position_enabled == defaults::kPositionEnabled);
    CHECK_NEAR(c.pos_limit_x, defaults::kPosLimitX);
    CHECK_NEAR(c.pos_limit_y, defaults::kPosLimitY);
    CHECK_NEAR(c.pos_limit_y_down, defaults::kPosLimitYDown);
    CHECK_NEAR(c.pos_limit_z, defaults::kPosLimitZ);
    CHECK_NEAR(c.pos_limit_z_back, defaults::kPosLimitZBack);
    CHECK(c.vk_toggle == defaults::kVkToggle);
    CHECK(c.vk_cycle_mode == defaults::kVkCycleMode);
    CHECK(c.vk_yaw_mode == defaults::kVkYawMode);
    CHECK(c.vk_ads_mode == defaults::kVkAdsMode);
    CHECK(c.chord_toggle == defaults::kChord);
    CHECK(c.chord_cycle_mode == defaults::kChord);
    CHECK(c.chord_yaw_mode == defaults::kChord);
    CHECK(c.chord_ads_mode == defaults::kChord);

    // Every section the reader looks in has to be in the file it wrote, or a key added
    // later lands in a section that is not there.
    const std::string text = ReadIni();
    for (const char* section : { "[General]", "[View]", "[Smoothing]", "[Position]",
                                 "[Hotkeys]" }) {
        CHECK(text.find(section) != std::string::npos);
    }
    std::remove(IniPath());
}

// A second load of the file the first one wrote must not drift.
void TestReloadIsIdempotent() {
    std::remove(IniPath());
    Config first;
    CHECK(first.LoadOrCreate(IniPath()));
    const std::string written = ReadIni();

    Config second;
    CHECK(second.LoadOrCreate(IniPath()));
    CHECK(ReadIni() == written);
    CHECK(second.udp_port == first.udp_port);
    CHECK(second.ads_mode == first.ads_mode);
    CHECK_NEAR(second.remote_smoothing, first.remote_smoothing);
    std::remove(IniPath());
}

void TestHandEditedValuesAreRead() {
    WriteIni("[General]\n"
             "EnableOnStartup=false\n"
             "Port=5555\n"
             "DataFreshnessMs=250\n"
             "WorldSpaceYaw=false\n"
             "ShowAimMarker=false\n"
             "AdsMode=tracked\n"
             "[View]\n"
             "Fov=95\n"
             "[Smoothing]\n"
             "LocalSmoothing=0.25\n"
             "RemoteSmoothing=0.4\n"
             "[Position]\n"
             "Enabled=false\n"
             "LimitX=0.11\n"
             "LimitY=0.22\n"
             "LimitYDown=0.33\n"
             "LimitZ=0.44\n"
             "LimitZBack=0.05\n"
             "PositionScale=-50\n"
             "[Hotkeys]\n"
             "Toggle=0x70\n"
             "CycleMode=0x71\n"
             "YawMode=0x72\n"
             "AdsMode=0x73\n"
             "ChordToggle=false\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK(!c.enabled_on_startup);
    CHECK(c.udp_port == 5555);
    CHECK(c.data_freshness_ms == 250);
    CHECK(!c.world_space_yaw);
    CHECK(!c.show_aim_marker);
    CHECK(c.ads_mode == AdsMode::Tracked);
    CHECK_NEAR(c.fov_override, 95.0f);
    CHECK_NEAR(c.local_smoothing, 0.25f);
    CHECK_NEAR(c.remote_smoothing, 0.4f);
    CHECK(!c.position_enabled);
    CHECK_NEAR(c.pos_limit_x, 0.11f);
    CHECK_NEAR(c.pos_limit_y, 0.22f);
    CHECK_NEAR(c.pos_limit_y_down, 0.33f);
    CHECK_NEAR(c.pos_limit_z, 0.44f);
    CHECK_NEAR(c.pos_limit_z_back, 0.05f);
    CHECK(c.vk_toggle == 0x70);
    CHECK(c.vk_cycle_mode == 0x71);
    CHECK(c.vk_yaw_mode == 0x72);
    CHECK(c.vk_ads_mode == 0x73);
    CHECK(!c.chord_toggle);
    CHECK(c.chord_cycle_mode);
    std::remove(IniPath());
}

// A port outside the range is the one config error that stops the load: the receiver
// cannot bind to it, so carrying on would leave the mod running with no tracker and
// nothing in the log tying that to the file.
void TestPortOutOfRangeFailsTheLoad() {
    for (const char* body : { "[General]\nPort=99999\n", "[General]\nPort=80\n" }) {
        WriteIni(body);
        Config c;
        CHECK(!c.LoadOrCreate(IniPath()));
    }
    std::remove(IniPath());
}

// Everything else that is out of range is repaired to the shipped value rather than
// failing the load, because a NaN or a negative limit reaches the view matrix.
void TestOutOfRangeValuesFallBackToTheDefaults() {
    WriteIni("[General]\n"
             "DataFreshnessMs=-5\n"
             "AdsMode=nonsense\n"
             "[View]\n"
             "Fov=200\n"
             "[Smoothing]\n"
             "LocalSmoothing=2.0\n"
             "RemoteSmoothing=-1.0\n"
             "[Position]\n"
             "LimitX=-0.5\n"
             "[Hotkeys]\n"
             "Toggle=0x1FF\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK(c.data_freshness_ms == defaults::kDataFreshnessMs);
    CHECK(c.ads_mode == defaults::kAdsMode);
    // Clamped into the range that has a projection, which is what someone typing 200 is
    // asking for.
    CHECK_NEAR(c.fov_override, defaults::kMaxFovOverride);
    CHECK_NEAR(c.local_smoothing, 1.0f);
    CHECK_NEAR(c.remote_smoothing, 0.0f);
    // A negative limit would hand PositionProcessor::ClampToLimits lo > hi, which pins
    // the offset at a constant instead of bounding it.
    CHECK_NEAR(c.pos_limit_x, 0.0f);
    CHECK(c.vk_toggle == defaults::kVkToggle);
    std::remove(IniPath());
}

// Zero is the off switch and passes through; a negative number is a typo for it rather
// than an angle, so it lands there too.
void TestFovOffAndNegativeBothRenderTheGamesOwn() {
    for (const char* body : { "[View]\nFov=0\n", "[View]\nFov=-3\n" }) {
        WriteIni(body);
        Config c;
        CHECK(c.LoadOrCreate(IniPath()));
        CHECK_NEAR(c.fov_override, defaults::kFovOverride);
    }
    std::remove(IniPath());
}

// A config that sets only LimitY must not keep the shipped 0.20 m of downward travel
// while the upward budget moves, with nothing in the log saying the key was
// half-effective.
void TestLimitYDownFollowsLimitYWhenUnset() {
    WriteIni("[Position]\nLimitY=0.45\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK_NEAR(c.pos_limit_y, 0.45f);
    CHECK_NEAR(c.pos_limit_y_down, 0.45f);
    std::remove(IniPath());
}

// The ADS mode is cycled in game and written back on every press. IniWriter truncates, so
// this one key has to go through the single-key path or the player loses every other
// setting and every comment in the file.
void TestSaveAdsModeKeepsTheRestOfTheFile() {
    std::remove(IniPath());
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    const std::string before = ReadIni();
    CHECK(before.find("AdsMode") != std::string::npos);

    Config::SaveAdsMode(AdsMode::Marker);
    const std::string after = ReadIni();
    CHECK(after.find("[Hotkeys]") != std::string::npos);
    CHECK(after.find("[Position]") != std::string::npos);
    CHECK(after.find("LimitZBack") != std::string::npos);
    // The comment block above the key is what tells the player what the three values
    // mean, and a truncating writer would take it with everything else.
    CHECK(after.find("paused   the game keeps the camera") != std::string::npos);

    Config reloaded;
    CHECK(reloaded.LoadOrCreate(IniPath()));
    CHECK(reloaded.ads_mode == AdsMode::Marker);
    std::remove(IniPath());
}

}  // namespace


// A decimal COMMA is what a player on a European desktop types, and the numeric reader
// stops at it: `LimitZ=0,4` parsed as 0, sanitised to a legal 0.0, and left forward lean
// dead for the session with nothing in the log - the range check only ever saw the value
// it would itself have chosen. The whole token has to parse, not a prefix of it.
void TestADecimalCommaIsRefusedRatherThanTruncated() {
    WriteIni("[Smoothing]\nLocalSmoothing=0,5\n[Position]\nLimitZ=0,4\nLimitX=0,3\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK_NEAR(c.local_smoothing, defaults::kLocalSmoothing);
    CHECK_NEAR(c.pos_limit_z, defaults::kPosLimitZ);
    CHECK_NEAR(c.pos_limit_x, defaults::kPosLimitX);
}

// Trailing text on the line is the same failure with a different cause.
void TestATrailingCommentOnAFloatIsRefused() {
    WriteIni("[Position]\nLimitZ=0.4 and a bit\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK_NEAR(c.pos_limit_z, defaults::kPosLimitZ);
}

// Non-finite text reaches the sanitiser intact - strtod parses both spellings - so the
// finite check is live code, and it was the one boundary check with no test at all.
void TestNonFiniteValuesFallBackToTheDefaults() {
    WriteIni("[Smoothing]\nLocalSmoothing=nan\nRemoteSmoothing=inf\n[Position]\nLimitZ=nan\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK_NEAR(c.local_smoothing, defaults::kLocalSmoothing);
    CHECK_NEAR(c.remote_smoothing, defaults::kRemoteSmoothing);
    CHECK_NEAR(c.pos_limit_z, defaults::kPosLimitZ);
}

// A bool the reader does not recognise falls back, and the fallback can be the OPPOSITE
// of what was typed, so a value that is genuinely not a yes/no keeps the default and says
// so rather than quietly flipping the setting.
void TestAnUnrecognisedBoolKeepsTheDefault() {
    WriteIni("[Position]\nEnabled=maybe\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK(c.position_enabled == defaults::kPositionEnabled);
    // The spellings the reader does accept still work.
    WriteIni("[Position]\nEnabled=FALSE\n");
    Config d;
    CHECK(d.LoadOrCreate(IniPath()));
    CHECK(!d.position_enabled);
}

// The shipped defaults as LITERALS. Comparing them against defaults::k* proves only that
// the writer and the reader agree; every one of those constants is an alias of a
// cameraunlock-core value, so a core bump could change what this mod ships with the suite
// still green.
void TestTheShippedDefaultsAreTheDocumentedNumbers() {
    std::remove(IniPath());
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK(c.udp_port == 4242);
    CHECK_NEAR(c.local_smoothing, 0.0f);
    CHECK_NEAR(c.remote_smoothing, 0.15f);
    CHECK_NEAR(c.pos_limit_x, 0.30f);
    CHECK_NEAR(c.pos_limit_y, 0.20f);
    CHECK_NEAR(c.pos_limit_y_down, 0.20f);
    CHECK_NEAR(c.pos_limit_z, 0.40f);
    CHECK_NEAR(c.pos_limit_z_back, 0.10f);
}


// A trailing comment is a documented-safe form on every value core reads as a number, and
// the whole-token check added for the decimal-comma case rejected it - turning an
// annotated config into a silent reset of every key the player had tuned.
void TestATrailingCommentIsKeptNotReset() {
    WriteIni("[Position]\nLimitZ=0.35 ; deeper lean\nLimitX=0.25 # sideways\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK_NEAR(c.pos_limit_z, 0.35f);
    CHECK_NEAR(c.pos_limit_x, 0.25f);
}

// Same for a bool, where the fallback is the OPPOSITE of what was typed. Core matches the
// whole string, so it read this as the default and switched positional tracking back on
// for someone who had just switched it off.
void TestABoolWithATrailingCommentIsHonoured() {
    WriteIni("[Position]\nEnabled=0 ; no lean\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK(!c.position_enabled);
}

// Core accepts exactly "true"/"True"/"TRUE" and nothing else, so any other casing read as
// the default with no warning. The mod decides these itself for that reason.
void TestBoolSpellingsAreCaseInsensitive() {
    WriteIni("[Position]\nEnabled=fAlSe\n[General]\nShowAimMarker=oFF\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK(!c.position_enabled);
    CHECK(!c.show_aim_marker);

    WriteIni("[Position]\nEnabled=YeS\n");
    Config d;
    CHECK(d.LoadOrCreate(IniPath()));
    CHECK(d.position_enabled);
}

// Port is the one fatal key in the file, so what it rejects matters more than anywhere
// else: a form core reads fine must not take the whole mod dormant for the session.
void TestPortAcceptsTheFormsCoreReads() {
    WriteIni("[General]\nPort=4243 ; tracker port\n");
    Config c;
    CHECK(c.LoadOrCreate(IniPath()));
    CHECK(c.udp_port == 4243);

    WriteIni("[General]\nPort=+4244\n");
    Config d;
    CHECK(d.LoadOrCreate(IniPath()));
    CHECK(d.udp_port == 4244);

    // Genuinely not a number is still fatal, which is the case the message was fixed for.
    WriteIni("[General]\nPort=default\n");
    Config e;
    CHECK(!e.LoadOrCreate(IniPath()));
}

int main() {
    TestFreshInstallRoundTripsTheShippedDefaults();
    TestReloadIsIdempotent();
    TestHandEditedValuesAreRead();
    TestPortOutOfRangeFailsTheLoad();
    TestOutOfRangeValuesFallBackToTheDefaults();
    TestFovOffAndNegativeBothRenderTheGamesOwn();
    TestLimitYDownFollowsLimitYWhenUnset();
    TestSaveAdsModeKeepsTheRestOfTheFile();
    TestADecimalCommaIsRefusedRatherThanTruncated();
    TestATrailingCommentOnAFloatIsRefused();
    TestNonFiniteValuesFallBackToTheDefaults();
    TestAnUnrecognisedBoolKeepsTheDefault();
    TestTheShippedDefaultsAreTheDocumentedNumbers();
    TestATrailingCommentIsKeptNotReset();
    TestABoolWithATrailingCommentIsHonoured();
    TestBoolSpellingsAreCaseInsensitive();
    TestPortAcceptsTheFormsCoreReads();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
