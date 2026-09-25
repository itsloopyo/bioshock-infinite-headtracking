// The differential test for the config conversion. Every input is read by:
//
//   the oracle  the reader of the newest published build, dev at 128e9d4, compiled from
//               oracle/ (byte copies of that commit's files, checked by tests/CMakeLists.txt)
//               against the core sources it was built with at c480d8a
//   the import  the frozen reader in src/legacy_config/
//
// and comparison 1 requires the two to agree on everything a player's file decides: whether
// the mod starts, every setting, the tracking state it starts in and the keys it binds. The
// one difference is DEV_DIFFERENCES below, with the commit that made it.
//
// Windows only: both readers are GetPrivateProfileString.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include "legacy_config/legacy_config.h"

// The oracle's own namespace is renamed on the way in, here and on its config.cpp (see
// tests/CMakeLists.txt), so its Config and the runtime's can live in one binary while its
// files stay byte copies of the published ones.
#define BioShockInfiniteHeadTracking BsiOracle128e9d4
#include "oracle/config.h"
#undef BioShockInfiniteHeadTracking

namespace {

namespace fs = std::filesystem;
namespace oracle = BsiOracle128e9d4;
namespace legacy = BioShockInfiniteHeadTracking::legacy;
using cameraunlock::TrackingMode;
using cameraunlock::config::LegacyKey;
using cameraunlock::config::testing::ChordSwitch;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

int g_failures = 0;
int g_checks = 0;

void Fail(const std::string& what) {
    ++g_failures;
    if (g_failures <= 200) std::printf("FAIL %s\n", what.c_str());
}

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) Fail(what);
}

// Everything a config file decides, as the game acts on it.
struct Effective {
    bool usable = false;
    bool enabled_on_startup = false;
    std::uint16_t udp_port = 0;
    int data_freshness_ms = 0;
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.0f;
    bool world_space_yaw = false;
    bool show_aim_marker = false;
    float fov_override = 0.0f;
    bool state_probe = false;
    bool aim_geometry_log = false;
    TrackingMode mode = TrackingMode::RotationAndPosition;
    float pos_limit_x = 0.0f;
    float pos_limit_y = 0.0f;
    float pos_limit_y_down = 0.0f;
    float pos_limit_z = 0.0f;
    float pos_limit_z_back = 0.0f;
    std::vector<KeyBinding> toggle;
    std::vector<KeyBinding> cycle_mode;
    std::vector<KeyBinding> yaw_mode;
};

// Hotkeys::Start at 128e9d4 and 4ad6dad: the nav key through NavGuarded, which does not
// fire while Ctrl and Shift are both held, and, where the chord switch is on, the letter
// through ChordGuarded, which fires only while they are. 0 is the poller's unbound.
std::vector<KeyBinding> LegacyBindings(int vk, bool chord, int letter) {
    std::vector<KeyBinding> out;
    if (vk != 0) out.push_back({KeyModifiers::kNone, vk});
    if (chord) out.push_back({KeyModifiers::kCtrl | KeyModifiers::kShift, letter});
    return out;
}

// TrackingRuntime::Start at 128e9d4 and 4ad6dad: [Position] Enabled picks the mode the
// session starts in, and the cycle then walks all three.
TrackingMode LegacyStartMode(bool position_enabled) {
    return position_enabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly;
}

template <class C>
Effective FromLegacyFields(bool usable, const C& c) {
    Effective e;
    e.usable = usable;
    e.enabled_on_startup = c.enabled_on_startup;
    e.udp_port = c.udp_port;
    e.data_freshness_ms = c.data_freshness_ms;
    e.local_smoothing = c.local_smoothing;
    e.remote_smoothing = c.remote_smoothing;
    e.world_space_yaw = c.world_space_yaw;
    e.show_aim_marker = c.show_aim_marker;
    e.fov_override = c.fov_override;
    e.state_probe = c.state_probe;
    e.aim_geometry_log = c.aim_geometry_log;
    e.mode = LegacyStartMode(c.position_enabled);
    e.pos_limit_x = c.pos_limit_x;
    e.pos_limit_y = c.pos_limit_y;
    e.pos_limit_y_down = c.pos_limit_y_down;
    e.pos_limit_z = c.pos_limit_z;
    e.pos_limit_z_back = c.pos_limit_z_back;
    e.toggle = LegacyBindings(c.vk_toggle, c.chord_toggle, 'Y');
    e.cycle_mode = LegacyBindings(c.vk_cycle_mode, c.chord_cycle_mode, 'G');
    e.yaw_mode = LegacyBindings(c.vk_yaw_mode, c.chord_yaw_mode, 'H');
    return e;
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

std::string Show(bool v) { return v ? "true" : "false"; }
std::string Show(int v) { return std::to_string(v); }
std::string Show(float v) {
    char text[64];
    std::snprintf(text, sizeof text, "%.9g", static_cast<double>(v));
    return text;
}
std::string Show(TrackingMode m) { return std::to_string(static_cast<int>(m)); }
std::string Show(const std::vector<KeyBinding>& list) {
    std::string out;
    for (const KeyBinding& b : list) {
        out += (out.empty() ? "" : ",") + std::to_string(static_cast<unsigned>(b.modifiers)) + ":" + std::to_string(b.vk);
    }
    return "[" + out + "]";
}

// One field of two readings that differ, by name.
struct FieldDifference {
    std::string field;
    std::string left;
    std::string right;
};

std::vector<FieldDifference> Differences(const Effective& a, const Effective& b) {
    std::vector<FieldDifference> out;
    const auto field = [&out](const char* name, bool same, std::string left, std::string right) {
        if (!same) out.push_back({name, std::move(left), std::move(right)});
    };
#define BSI_FIELD(f) field(#f, a.f == b.f, Show(a.f), Show(b.f))
#define BSI_FLOAT(f) field(#f, SameBits(a.f, b.f), Show(a.f), Show(b.f))
    BSI_FIELD(usable);
    if (!a.usable || !b.usable) return out;
    BSI_FIELD(enabled_on_startup);
    field("udp_port", a.udp_port == b.udp_port, Show(static_cast<int>(a.udp_port)), Show(static_cast<int>(b.udp_port)));
    BSI_FIELD(data_freshness_ms);
    BSI_FLOAT(local_smoothing);
    BSI_FLOAT(remote_smoothing);
    BSI_FIELD(world_space_yaw);
    BSI_FIELD(show_aim_marker);
    BSI_FLOAT(fov_override);
    BSI_FIELD(state_probe);
    BSI_FIELD(aim_geometry_log);
    BSI_FIELD(mode);
    BSI_FLOAT(pos_limit_x);
    BSI_FLOAT(pos_limit_y);
    BSI_FLOAT(pos_limit_y_down);
    BSI_FLOAT(pos_limit_z);
    BSI_FLOAT(pos_limit_z_back);
    BSI_FIELD(toggle);
    BSI_FIELD(cycle_mode);
    BSI_FIELD(yaw_mode);
#undef BSI_FIELD
#undef BSI_FLOAT
    return out;
}

// ---- files ------------------------------------------------------------------------------

fs::path g_root;
int g_next_dir = 0;

fs::path FreshDir() {
    const fs::path dir = g_root / std::to_string(g_next_dir++);
    fs::create_directories(dir);
    return dir;
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A corpus input: its name, and its bytes, or none for the no-file case.
struct Input {
    std::string name;
    bool present = true;
    std::string bytes;
};

// ---- the readers ------------------------------------------------------------------------

// The published build: LoadOrCreate, which also writes the default file where there is
// none, so it runs in a folder of its own.
Effective ReadOracle(const Input& input) {
    const fs::path path = FreshDir() / "HeadTracking.ini";
    if (input.present) WriteBytes(path, input.bytes);
    oracle::Config c;
    const bool usable = c.LoadOrCreate(path.string().c_str());
    return FromLegacyFields(usable, c);
}

Effective ReadImport(const Input& input) {
    const fs::path path = FreshDir() / "HeadTracking.ini";
    if (input.present) WriteBytes(path, input.bytes);
    legacy::Config c;
    const bool usable = legacy::Read(path.string().c_str(), c);
    return FromLegacyFields(usable, c);
}

// ---- the inputs -------------------------------------------------------------------------

// Every key the frozen reader takes a value from, described for the corpus generator. The
// retired keys it only names in the log (the old sensitivity, recentre and ADS keys) are
// not here: no value of theirs reaches anything.
std::vector<MutationKey> KeyDescriptors() {
    const auto key = [](const char* section, const char* name, const char* alternate,
                        std::vector<std::string> out_of_range = {}) {
        MutationKey k;
        k.section = section;
        k.key = name;
        k.alternate = alternate;
        k.out_of_range = std::move(out_of_range);
        return k;
    };
    const auto hotkey = [](const char* name, const char* alternate, const char* chord) {
        MutationKey k;
        k.section = "Hotkeys";
        k.key = name;
        k.alternate = alternate;
        k.out_of_range = {"0xFF", "0x1FF", "-1"};
        k.hotkey = true;
        k.chords = {ChordSwitch{"Hotkeys", chord, "true", "false"}};
        return k;
    };
    return {
        key("General", "EnableOnStartup", "false"),
        key("General", "Port", "4243", {"1023", "65536"}),
        key("General", "DataFreshnessMs", "250", {"0"}),
        key("General", "WorldSpaceYaw", "false"),
        key("General", "ShowAimMarker", "false"),
        key("View", "Fov", "95", {"-3", "20", "151"}),
        key("Smoothing", "LocalSmoothing", "0.25", {"-0.5", "1.5"}),
        key("Smoothing", "RemoteSmoothing", "0.4", {"-0.5", "1.5"}),
        key("Position", "Enabled", "false"),
        key("Position", "LimitX", "0.25", {"-0.5"}),
        key("Position", "LimitY", "0.3", {"-0.5"}),
        key("Position", "LimitYDown", "0.1", {"-0.5"}),
        key("Position", "LimitZ", "0.35", {"-0.5"}),
        key("Position", "LimitZBack", "0.05", {"-0.5"}),
        hotkey("Toggle", "0x70", "ChordToggle"),
        hotkey("CycleMode", "0x71", "ChordCycleMode"),
        hotkey("YawMode", "0x72", "ChordYawMode"),
        key("Hotkeys", "ChordToggle", "false"),
        key("Hotkeys", "ChordCycleMode", "false"),
        key("Hotkeys", "ChordYawMode", "false"),
        key("Diagnostics", "StateProbe", "true"),
        key("Diagnostics", "AimGeometry", "true"),
    };
}

std::vector<LegacyKey> ReadKeys() {
    std::vector<LegacyKey> out;
    for (const MutationKey& k : KeyDescriptors()) out.push_back({k.section, k.key});
    return out;
}

fs::path DataPath(const char* name) { return fs::path(BSI_DIFFERENTIAL_DATA_DIR) / name; }

// What the dev build wrote at its first launch, taken once from the oracle and committed.
std::string DevFirstRun() { return ReadBytes(DataPath("dev-128e9d4-first-run.ini")); }

std::vector<Input> Inputs() {
    const std::string first_run = DevFirstRun();
    std::vector<Input> out;
    out.push_back({"dev first run", true, first_run});
    out.push_back({"no file", false, {}});
    out.push_back({"empty file", true, {}});

    // Written by the dev build itself: it saved the ADS mode on every press of Insert.
    for (const char* mode : {"marker", "tracked"}) {
        std::string bytes = first_run;
        const std::string from = "AdsMode=paused";
        bytes.replace(bytes.find(from), from.size(), std::string("AdsMode=") + mode);
        out.push_back({std::string("dev first run, AdsMode saved as ") + mode, true, bytes});
    }
    // A position limit past the canonical 0-10 m range, which the published build took.
    for (const char* limit : {"LimitX=11", "LimitZ=100"}) {
        std::string bytes = first_run;
        const std::string key = std::string(limit).substr(0, std::string(limit).find('=') + 1);
        const std::size_t at = bytes.find(key);
        bytes.replace(at, bytes.find('\r', at) - at, limit);
        out.push_back({std::string("dev first run, ") + limit, true, bytes});
    }

    for (IniMutation& m : cameraunlock::config::testing::GenerateIniMutations(first_run, ReadKeys(),
                                                                            KeyDescriptors())) {
        out.push_back({std::move(m.name), true, std::move(m.bytes)});
    }
    return out;
}

// ---- the tests --------------------------------------------------------------------------

// The oracle's first-run output is what the committed test data says it is, so the data
// cannot drift from the build it stands for.
void TestTheCommittedFirstRunIsTheDevBuilds() {
    const fs::path path = FreshDir() / "HeadTracking.ini";
    oracle::Config c;
    Check(c.LoadOrCreate(path.string().c_str()), "the oracle creates its first-run file");
    Check(ReadBytes(path) == DevFirstRun(), "dev-128e9d4-first-run.ini is the oracle's first-run output");
}

// Comparison 1. Every commit between 128e9d4 and the import that changed how the file is
// read, and what it changed. The comparison below is over the fields both readers have, so
// these are the whole of what a player updating from the dev build sees change that the
// conversion did not cause.
const char* const DEV_DIFFERENCES[] = {
    "78cb86e feat(ads)!: [General] AdsMode, [Hotkeys] AdsMode and [Hotkeys] ChordAdsMode are no "
    "longer read. The dev build paused head tracking while aiming by default and bound Insert and "
    "Ctrl+Shift+U to cycle that; head tracking now stays on through the aim and neither key is bound.",
};

void TestComparisonOneOracleAgainstImport(const std::vector<Input>& inputs) {
    for (const Input& input : inputs) {
        const Effective o = ReadOracle(input);
        const Effective i = ReadImport(input);
        for (const FieldDifference& d : Differences(o, i)) {
            Fail(input.name + ": " + d.field + " oracle=" + d.left + " import=" + d.right);
        }
        ++g_checks;
    }
}

}  // namespace

int main(int argc, char** argv) {
    // How data/dev-128e9d4-first-run.ini was made: the oracle's first launch, in a folder
    // with no file. TestTheCommittedFirstRunIsTheDevBuilds holds the data to it.
    if (argc == 3 && std::strcmp(argv[1], "--write-dev-first-run") == 0) {
        oracle::Config c;
        return c.LoadOrCreate(argv[2]) ? 0 : 1;
    }

    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    g_root = fs::path(temp) / ("bsi-config-differential-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(g_root);
    fs::create_directories(g_root);

    int exit_code = 1;
    try {
        TestTheCommittedFirstRunIsTheDevBuilds();
        const std::vector<Input> inputs = Inputs();
        TestComparisonOneOracleAgainstImport(inputs);
        std::printf("%zu inputs. Differences from the dev build that the comparison allows for:\n", inputs.size());
        for (const char* difference : DEV_DIFFERENCES) std::printf("  %s\n", difference);
        std::printf("%d checks, %d failures\n", g_checks, g_failures);
        exit_code = g_failures == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
    }
    fs::remove_all(g_root);
    return exit_code;
}
