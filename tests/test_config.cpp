// HeadTracking.ini as the canonical config format: the file the mod creates, what its
// table reads, which rows a hotkey may save, and that a save touches nothing else.
//
// The conversion of an older file is tests/config_differential/'s job; this suite covers
// the canonical file from its first launch on.
//
// `bsi_config_tests --render-config <path>` writes the file the table renders from its
// defaults to <path> and runs nothing else. `pixi run render-config` uses it to rewrite
// config/HeadTracking.ini, which TestTheCommittedFileIsTheRenderedDefaults holds to the code.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

namespace fs = std::filesystem;
using namespace BioShockInfiniteHeadTracking;
using cameraunlock::config::ConfigLoadStatus;
using cameraunlock::config::ConfigOwner;
using cameraunlock::config::ConfigOwnerOptions;
using cameraunlock::config::ConfigSaveStatus;

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what, int line) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s\n", __FILE__, line, what);
}

#define CHECK(cond) Check((cond), #cond, __LINE__)

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("could not write " + path.string());
}

std::string Rendered(const Config& config) {
    return cameraunlock::config::RenderCanonical(ConfigTable(), config, ConfigHeader());
}

fs::path g_root;
int g_next_dir = 0;

fs::path FreshIni() {
    const fs::path dir = g_root / std::to_string(g_next_dir++);
    fs::create_directories(dir);
    return dir / "HeadTracking.ini";
}

std::unique_ptr<ConfigOwner<Config>> NewOwner(const fs::path& path) {
    ConfigOwnerOptions<Config> options;
    options.path = path.wstring();
    options.table = ConfigTable();
    options.import = ConfigLegacyImport();
    options.header = ConfigHeader();
    return std::make_unique<ConfigOwner<Config>>(std::move(options));
}

// The lines of `after` that differ from `before`, which must have as many lines.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const auto split = [](const std::string& text) {
        std::vector<std::string> lines;
        std::size_t start = 0;
        for (std::size_t end; (end = text.find("\r\n", start)) != std::string::npos; start = end + 2) {
            lines.push_back(text.substr(start, end - start));
        }
        lines.push_back(text.substr(start));
        return lines;
    };
    const std::vector<std::string> a = split(before);
    const std::vector<std::string> b = split(after);
    std::vector<std::string> changed;
    if (a.size() != b.size()) {
        changed.push_back("line count");
        return changed;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

// ---- the file --------------------------------------------------------------------------

void TestTheCommittedFileIsTheRenderedDefaults() {
    CHECK(ReadBytes(BSI_COMMITTED_CONFIG) == Rendered(ConfigTable().defaults()));
}

// The shipped defaults as literals, so a core bump that moves one of core's constants
// shows up here rather than in a player's file.
void TestTheDefaultsAreTheDocumentedNumbers() {
    const Config c = ConfigTable().defaults();
    CHECK(c.udp_port == 4242);
    CHECK(c.enable_on_startup);
    CHECK(c.world_space_yaw);
    CHECK(c.data_freshness_ms == 500);
    CHECK(c.local_smoothing == 0.0f);
    CHECK(c.remote_smoothing == 0.15f);
    CHECK(c.rotation_enabled && c.position_enabled);
    CHECK(c.pos_limit_x == 0.30f);
    CHECK(c.pos_limit_y == 0.20f);
    CHECK(c.pos_limit_y_down == 0.20f);
    CHECK(c.pos_limit_z == 0.40f);
    CHECK(c.pos_limit_z_back == 0.10f);
    CHECK(c.toggle_key == "End, Ctrl+Shift+Y");
    CHECK(c.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G");
    CHECK(c.yaw_mode_key == "PageDown, Ctrl+Shift+H");
    CHECK(c.fov_override == 0.0f);
    CHECK(!c.state_probe);
    CHECK(!c.aim_geometry_log);
}

// Every key list parses as native key bindings, chords included, since the hotkeys are
// registered straight from them.
void TestTheDefaultKeyListsParse() {
    const Config c = ConfigTable().defaults();
    for (const std::string* list : {&c.toggle_key, &c.cycle_tracking_mode_key, &c.yaw_mode_key}) {
        const auto parsed = cameraunlock::input::ParseKeyBindings(*list);
        CHECK(parsed.ok());
        CHECK(parsed.bindings.size() == 2);
    }
}

// ---- what the table reads -------------------------------------------------------------

Config Applied(const std::string& body, std::size_t* diagnostics = nullptr) {
    const std::string bytes = "[CameraUnlock]\r\nConfigFormat=1\r\n" + body;
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(bytes);
    Config config = ConfigTable().defaults();
    const auto report = cameraunlock::config::ApplyCanonical(doc, ConfigTable(), config);
    if (diagnostics) *diagnostics = doc.diagnostics.size() + report.diagnostics.size();
    return config;
}

void TestHandEditedValuesAreRead() {
    std::size_t diagnostics = 1;
    const Config c = Applied("[Network]\r\nUdpPort=5555\r\n"
                             "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\n"
                             "RotationEnabled=true\r\nDataFreshnessMs=250\r\n"
                             "[Smoothing]\r\nLocalSmoothing=0.25\r\nRemoteSmoothing=0.4\r\n"
                             "[Position]\r\nPositionEnabled=false\r\nPositionLimitX=0.11\r\n"
                             "PositionLimitY=0.22\r\nPositionLimitYDown=0.33\r\nPositionLimitZ=0.44\r\n"
                             "PositionLimitZBack=0.05\r\n"
                             "[Hotkeys]\r\nToggleKey=F1\r\nCycleTrackingModeKey=F2, Ctrl+Shift+G\r\n"
                             "YawModeKey=\r\n"
                             "[View]\r\nFov=95\r\n"
                             "[Diagnostics]\r\nStateProbe=true\r\nAimGeometry=true\r\n",
                             &diagnostics);
    CHECK(diagnostics == 0);
    CHECK(c.udp_port == 5555);
    CHECK(!c.enable_on_startup);
    CHECK(!c.world_space_yaw);
    CHECK(c.data_freshness_ms == 250);
    CHECK(c.local_smoothing == 0.25f);
    CHECK(c.remote_smoothing == 0.4f);
    CHECK(c.rotation_enabled && !c.position_enabled);
    CHECK(c.pos_limit_x == 0.11f);
    CHECK(c.pos_limit_y == 0.22f);
    CHECK(c.pos_limit_y_down == 0.33f);
    CHECK(c.pos_limit_z == 0.44f);
    CHECK(c.pos_limit_z_back == 0.05f);
    CHECK(c.toggle_key == "F1");
    CHECK(c.cycle_tracking_mode_key == "F2, Ctrl+Shift+G");
    CHECK(c.yaw_mode_key.empty());
    CHECK(c.fov_override == 95.0f);
    CHECK(c.state_probe);
    CHECK(c.aim_geometry_log);
}

// [View] Fov is 0 or an angle from 30 to 150. Anything else keeps the default, the game's
// own angle, with a diagnostic, rather than being clamped into the range.
void TestFovOutsideItsRangeKeepsTheGamesAngle() {
    for (const char* value : {"20", "-3", "151", "0,5", "nan"}) {
        std::size_t diagnostics = 0;
        const Config c = Applied(std::string("[View]\r\nFov=") + value + "\r\n", &diagnostics);
        CHECK(c.fov_override == 0.0f);
        CHECK(diagnostics == 1);
    }
    for (const float angle : {0.0f, 30.0f, 150.0f}) {
        CHECK(FovCodec{}.Parse(FovCodec{}.Render(angle)).value == angle);
    }
    bool threw = false;
    try {
        FovCodec{}.Render(20.0f);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

// The pre-canonical keys are not read from a canonical file: a player who copies an old
// line in gets a diagnostic, not a setting.
void TestThePreCanonicalKeysAreNotRead() {
    std::size_t diagnostics = 0;
    const Config c = Applied("[General]\r\nShowAimMarker=false\r\n[Hotkeys]\r\nChordToggle=false\r\n",
                             &diagnostics);
    CHECK(diagnostics == 2);
    CHECK(c.toggle_key == "End, Ctrl+Shift+Y");
}

// ---- what the owner writes --------------------------------------------------------------

void TestFirstLaunchCreatesTheCommittedFile() {
    const fs::path path = FreshIni();
    const auto owner = NewOwner(path);
    CHECK(owner->Load().status == ConfigLoadStatus::Created);
    CHECK(ReadBytes(path) == ReadBytes(BSI_COMMITTED_CONFIG));
}

// A save changes the lines of its rows and not one other byte, and the next launch reads
// what was saved. The tracking mode is always written as the pair.
void TestTheModeAndYawKeysSaveTheirRowsOnly() {
    const fs::path path = FreshIni();
    const auto owner = NewOwner(path);
    owner->Load();
    const std::string created = ReadBytes(path);

    const cameraunlock::TrackingModeChannels positionOnly =
        cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::PositionOnly);
    CHECK(owner->Save([&](Config& c) {
                   c.rotation_enabled = positionOnly.rotation_enabled;
                   c.position_enabled = positionOnly.position_enabled;
               }).status == ConfigSaveStatus::Saved);
    const std::string moded = ReadBytes(path);
    CHECK((ChangedLines(created, moded) == std::vector<std::string>{"RotationEnabled=false"}));

    const cameraunlock::TrackingModeChannels rotationOnly =
        cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
    CHECK(owner->Save([&](Config& c) {
                   c.rotation_enabled = rotationOnly.rotation_enabled;
                   c.position_enabled = rotationOnly.position_enabled;
               }).status == ConfigSaveStatus::Saved);
    const std::string cycled = ReadBytes(path);
    CHECK((ChangedLines(created, cycled) == std::vector<std::string>{"PositionEnabled=false"}));

    CHECK(owner->Save([](Config& c) { c.world_space_yaw = false; }).status == ConfigSaveStatus::Saved);
    const std::string yawed = ReadBytes(path);
    CHECK((ChangedLines(cycled, yawed) == std::vector<std::string>{"WorldSpaceYaw=false"}));


    const auto next = NewOwner(path);
    const auto loaded = next->Load();
    CHECK(loaded.status == ConfigLoadStatus::Canonical);
    CHECK(loaded.config.rotation_enabled && !loaded.config.position_enabled);
    CHECK(!loaded.config.world_space_yaw);
    CHECK(ReadBytes(path) == yawed);
}

// End changes the session only, so EnableOnStartup is not a row a save may change; nor is
// anything else a hotkey does not set.
void TestOnlyTheModeAndYawRowsAreWritable() {
    const fs::path path = FreshIni();
    const auto owner = NewOwner(path);
    owner->Load();
    const std::string created = ReadBytes(path);
    int refused = 0;
    const std::vector<void (*)(Config&)> changes = {
        [](Config& c) { c.enable_on_startup = false; },
        [](Config& c) { c.udp_port = 5555; },
        [](Config& c) { c.toggle_key = "F1"; },
        [](Config& c) { c.fov_override = 90.0f; },
    };
    for (const auto change : changes) {
        try {
            owner->Save(change);
        } catch (const std::logic_error&) {
            ++refused;
        }
    }
    CHECK(refused == static_cast<int>(changes.size()));
    CHECK(ReadBytes(path) == created);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        WriteBytes(argv[2], Rendered(ConfigTable().defaults()));
        return 0;
    }

    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    g_root = fs::path(temp) / ("bsi-config-tests-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(g_root);
    fs::create_directories(g_root);

    int exit_code = 1;
    try {
        TestTheCommittedFileIsTheRenderedDefaults();
        TestTheDefaultsAreTheDocumentedNumbers();
        TestTheDefaultKeyListsParse();
        TestHandEditedValuesAreRead();
        TestFovOutsideItsRangeKeepsTheGamesAngle();
        TestThePreCanonicalKeysAreNotRead();
        TestFirstLaunchCreatesTheCommittedFile();
        TestTheModeAndYawKeysSaveTheirRowsOnly();
        TestOnlyTheModeAndYawRowsAreWritable();
        std::printf("%d checks, %d failures\n", g_checks, g_failures);
        exit_code = g_failures == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
    }
    fs::remove_all(g_root);
    return exit_code;
}
