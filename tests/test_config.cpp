// CameraUnlock.ini, the canonical config file: the file the mod creates, what its table
// reads, which rows a hotkey may save, that a save touches nothing else, and how the rows
// set to default follow Defaults.ini.
//
// The import of HeadTracking.ini, the file earlier versions read, is
// tests/config_differential/'s job; this suite covers CameraUnlock.ini from its first launch on.
//
// `bsi_config_tests --render-config <path>` writes the table's fresh render to <path> and
// runs nothing else. `pixi run render-config` uses it to rewrite config/HeadTracking.ini, the
// committed copy, which TestTheCommittedFileIsTheFreshRender holds to the code.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
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
using cameraunlock::config::ConfigSaveStatus;
using cameraunlock::config::DefaultsFile;

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

std::string FreshRender() { return cameraunlock::config::RenderCanonicalFresh(ConfigTable(), ConfigHeader()); }

fs::path g_root;
int g_next_dir = 0;

// A game folder of its own, and a Defaults.ini of its own outside it, which the first Load
// creates at the built-in values unless the test writes one first.
struct Folder {
    fs::path dir;
    fs::path config;
    fs::path legacy;
    fs::path defaults;
};

Folder FreshFolder() {
    Folder f;
    const fs::path root = g_root / std::to_string(g_next_dir++);
    f.dir = root / "game";
    fs::create_directories(f.dir);
    fs::create_directories(root / "user");
    f.config = f.dir / "CameraUnlock.ini";
    f.legacy = f.dir / "HeadTracking.ini";
    f.defaults = root / "user" / "CameraUnlock" / "Defaults.ini";
    return f;
}

std::unique_ptr<ConfigOwner<Config>> NewOwner(const Folder& f) {
    return std::make_unique<ConfigOwner<Config>>(
        ConfigOptions(f.dir.wstring() + L"\\", DefaultsFile::At(f.defaults.wstring())));
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
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

void TestTheCommittedFileIsTheFreshRender() {
    CHECK(ReadBytes(BSI_COMMITTED_CONFIG) == FreshRender());
}

// Every global row the game binds is written as default, and the game's own rows as values.
void TestTheFreshFileFollowsDefaultsIniOnEveryGlobalRow() {
    const std::string fresh = FreshRender();
    for (const char* line : {"UdpPort=default", "EnableOnStartup=default", "WorldSpaceYaw=default",
                             "RotationEnabled=default", "DataFreshnessMs=default", "LocalSmoothing=default",
                             "RemoteSmoothing=default", "PositionEnabled=default", "PositionLimitX=default",
                             "PositionLimitY=default", "PositionLimitYDown=default", "PositionLimitZ=default",
                             "PositionLimitZBack=default", "ToggleKey=default", "CycleTrackingModeKey=default",
                             "YawModeKey=default", "Fov=0.0", "StateProbe=false", "AimGeometry=false"}) {
        CHECK(fresh.find(std::string("\r\n") + line + "\r\n") != std::string::npos);
    }
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

// With no file in the folder, the first launch creates CameraUnlock.ini as committed and
// Defaults.ini at the built-in values, runs on those values, and creates no HeadTracking.ini.
void TestFirstLaunchCreatesTheCommittedFile() {
    const Folder f = FreshFolder();
    const auto loaded = NewOwner(f)->Load();
    CHECK(loaded.status == ConfigLoadStatus::Created);
    CHECK(ReadBytes(f.config) == ReadBytes(BSI_COMMITTED_CONFIG));
    CHECK(fs::exists(f.defaults));
    CHECK(!fs::exists(f.legacy));
    CHECK(cameraunlock::config::RenderCanonical(ConfigTable(), loaded.config, ConfigHeader()) ==
          cameraunlock::config::RenderCanonical(ConfigTable(), ConfigTable().defaults(), ConfigHeader()));
}

// A row set to default takes Defaults.ini's value, the tracking mode pair included, and a
// row the file sets keeps its own.
void TestDefaultRowsFollowDefaultsIni() {
    const Folder f = FreshFolder();
    NewOwner(f)->Load();
    std::string global = ReadBytes(f.defaults);
    for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
             {"UdpPort=4242", "UdpPort=5000"},
             {"WorldSpaceYaw=true", "WorldSpaceYaw=false"},
             {"PositionEnabled=true", "PositionEnabled=false"},
             {"PositionLimitZ=0.4", "PositionLimitZ=0.25"},
             {"ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=F8"}}) {
        const std::size_t at = global.find(from + "\r\n");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) global.replace(at, from.size(), to);
    }
    WriteBytes(f.defaults, global);
    std::string file = ReadBytes(f.config);
    const std::string yaw = "WorldSpaceYaw=default";
    file.replace(file.find(yaw), yaw.size(), "WorldSpaceYaw=true");
    WriteBytes(f.config, file);

    const auto loaded = NewOwner(f)->Load();
    CHECK(loaded.status == ConfigLoadStatus::Canonical);
    CHECK(loaded.config.udp_port == 5000);
    CHECK(loaded.config.world_space_yaw);
    CHECK(loaded.config.rotation_enabled && !loaded.config.position_enabled);
    CHECK(loaded.config.pos_limit_z == 0.25f);
    CHECK(loaded.config.toggle_key == "F8");
    CHECK(ReadBytes(f.config) == file);
    CHECK(ReadBytes(f.defaults) == global);
}

// A save changes the lines of its rows and not one other byte, and the next launch reads
// what was saved. The tracking mode is always written as the pair. A row that held default
// is written as a value, the log says it no longer follows Defaults.ini, and Defaults.ini is
// never written.
void TestTheModeAndYawKeysSaveTheirRowsOnly() {
    const Folder f = FreshFolder();
    const fs::path& path = f.config;
    const auto owner = NewOwner(f);
    owner->Load();
    const std::string created = ReadBytes(path);
    const std::string global = ReadBytes(f.defaults);

    const cameraunlock::TrackingModeChannels positionOnly =
        cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::PositionOnly);
    const auto modeSave = owner->Save([&](Config& c) {
        c.rotation_enabled = positionOnly.rotation_enabled;
        c.position_enabled = positionOnly.position_enabled;
    });
    CHECK(modeSave.status == ConfigSaveStatus::Saved);
    CHECK(Contains(modeSave.log, "RotationEnabled=false is now set for this game, and no longer follows Defaults.ini"));
    const std::string moded = ReadBytes(path);
    CHECK((ChangedLines(created, moded) == std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"}));

    const cameraunlock::TrackingModeChannels rotationOnly =
        cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
    CHECK(owner->Save([&](Config& c) {
                   c.rotation_enabled = rotationOnly.rotation_enabled;
                   c.position_enabled = rotationOnly.position_enabled;
               }).status == ConfigSaveStatus::Saved);
    const std::string cycled = ReadBytes(path);
    CHECK((ChangedLines(created, cycled) == std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=false"}));

    const auto yawSave = owner->Save([](Config& c) { c.world_space_yaw = false; });
    CHECK(yawSave.status == ConfigSaveStatus::Saved);
    CHECK(Contains(yawSave.log, "WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini"));
    const std::string yawed = ReadBytes(path);
    CHECK((ChangedLines(cycled, yawed) == std::vector<std::string>{"WorldSpaceYaw=false"}));
    CHECK(ReadBytes(f.defaults) == global);

    const auto next = NewOwner(f);
    const auto loaded = next->Load();
    CHECK(loaded.status == ConfigLoadStatus::Canonical);
    CHECK(loaded.config.rotation_enabled && !loaded.config.position_enabled);
    CHECK(!loaded.config.world_space_yaw);
    CHECK(ReadBytes(path) == yawed);
}

// End changes the session only, so EnableOnStartup is not a row a save may change; nor is
// anything else a hotkey does not set.
void TestOnlyTheModeAndYawRowsAreWritable() {
    const Folder f = FreshFolder();
    const fs::path& path = f.config;
    const auto owner = NewOwner(f);
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
        WriteBytes(argv[2], FreshRender());
        return 0;
    }

    char temp[MAX_PATH];
    GetTempPathA(MAX_PATH, temp);
    g_root = fs::path(temp) / ("bsi-config-tests-" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(g_root);
    fs::create_directories(g_root);

    int exit_code = 1;
    try {
        TestTheCommittedFileIsTheFreshRender();
        TestTheFreshFileFollowsDefaultsIniOnEveryGlobalRow();
        TestTheDefaultsAreTheDocumentedNumbers();
        TestTheDefaultKeyListsParse();
        TestHandEditedValuesAreRead();
        TestFovOutsideItsRangeKeepsTheGamesAngle();
        TestThePreCanonicalKeysAreNotRead();
        TestFirstLaunchCreatesTheCommittedFile();
        TestDefaultRowsFollowDefaultsIni();
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
