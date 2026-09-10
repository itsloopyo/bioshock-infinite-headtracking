// The tracker port is shared with every other head tracking mod we ship, and with
// OpenTrack itself. A player who launches this game while the last one is still
// running hands us a port we cannot have, and the only thing that ever gets it back
// is the receiver's own retry - nothing re-runs Start().
//
// This drives the real TrackingRuntime through that exact sequence against real
// sockets: another process holds the port, the mod starts anyway, the holder exits,
// and tracking has to come up on its own. It exists because the failure it guards
// against is a one-line refactor away (a `if (!Start(...)) return;` in whatever calls
// this) and is invisible in game - the mod simply never tracks, with a log line
// thirty seconds up the file explaining why.
//
// Windows-only, unlike test_main.cpp: there is no way to test a socket race without
// sockets. Until this repo has a build system, run it from the repo root with
/*
   g++ -std=c++17 -Isrc -Icameraunlock-core/cpp/include tests/test_port_reclaim.cpp \
       src/tracking_runtime.cpp src/config.cpp \
       cameraunlock-core/cpp/src/protocol/udp_receiver.cpp \
       cameraunlock-core/cpp/src/protocol/udp_socket.cpp \
       cameraunlock-core/cpp/src/protocol/opentrack_packet.cpp \
       cameraunlock-core/cpp/src/data/tracking_pose.cpp \
       cameraunlock-core/cpp/src/logging/file_log.cpp \
       cameraunlock-core/cpp/src/config/ini_reader.cpp \
       -lws2_32 -o port_reclaim_tests.exe
*/
// Exit code 0 means every check passed.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "tracking_runtime.h"

#include "cameraunlock/protocol/udp_receiver.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) {
        std::printf("  [PASS] %s\n", what);
        return;
    }
    ++g_failures;
    std::printf("  [FAIL] %s\n", what);
}

using Clock = std::chrono::steady_clock;

int ElapsedMs(Clock::time_point since) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count());
}

// The previous game's mod, still holding the port. Raw winsock rather than a
// UdpReceiver so this stays a test of one receiver against a foreign holder.
SOCKET OpenHolder(uint16_t port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

// 48-byte OpenTrack datagram: x, y, z, yaw, pitch, roll as doubles.
void BuildPacket(unsigned char out[48], double yaw, double pitch, double roll) {
    const double zero = 0.0;
    std::memcpy(out + 0, &zero, sizeof(double));
    std::memcpy(out + 8, &zero, sizeof(double));
    std::memcpy(out + 16, &zero, sizeof(double));
    std::memcpy(out + 24, &yaw, sizeof(double));
    std::memcpy(out + 32, &pitch, sizeof(double));
    std::memcpy(out + 40, &roll, sizeof(double));
}

bool SendPose(SOCKET sender, uint16_t port, double yaw) {
    unsigned char pkt[48];
    BuildPacket(pkt, yaw, 0.0, 0.0);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    return sendto(sender, reinterpret_cast<const char*>(pkt), sizeof(pkt), 0,
                  reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == sizeof(pkt);
}

// Frames the game would have rendered while the player was closing the other one:
// the pose only ever reaches the camera through SampleFrame, so that is what has to
// come alive, not merely the socket.
bool PumpUntilTracking(BioShockInfiniteHeadTracking::TrackingRuntime& runtime,
                       SOCKET sender, uint16_t port, int timeoutMs, int& elapsedMsOut) {
    const Clock::time_point start = Clock::now();
    double yaw = 1.0;
    while (ElapsedMs(start) < timeoutMs) {
        // Walk the yaw so the pose is never a bit-identical repeat, which the
        // receiver's dropout gate would (correctly) treat as a frozen tracker.
        SendPose(sender, port, yaw);
        yaw = yaw >= 4.0 ? 1.0 : yaw + 1.0;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (runtime.SampleFrame().has_rotation) {
            elapsedMsOut = ElapsedMs(start);
            return true;
        }
    }
    elapsedMsOut = ElapsedMs(start);
    return false;
}

// The receiver logs from two threads - Start() on the caller's, the retry from the
// supervisor's - so the sink a mod installs has to be safe for both, and so does this.
class LogCapture {
public:
    std::function<void(const std::string&)> Sink() {
        return [this](const std::string& line) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lines.push_back(line);
        };
    }

    bool AnyLineContains(const std::string& needle) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const std::string& line : m_lines) {
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    std::string First() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lines.empty() ? std::string() : m_lines.front();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_lines;
};

}  // namespace

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::printf("WSAStartup failed\n");
        return 1;
    }

    // A fixed port flakes when a previous run's socket has not been reaped, so walk
    // for one nothing else is on.
    uint16_t port = 0;
    for (uint16_t candidate = 14311; candidate < 14311 + 32; ++candidate) {
        SOCKET probe = OpenHolder(candidate);
        if (probe != INVALID_SOCKET) {
            closesocket(probe);
            port = candidate;
            break;
        }
    }
    if (port == 0) {
        std::printf("no free test port in 14311-14342\n");
        return 1;
    }

    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender == INVALID_SOCKET) {
        std::printf("sender socket failed\n");
        return 1;
    }

    std::printf("Port reclaim tests (port %u):\n", port);

    SOCKET holder = OpenHolder(port);
    Check(holder != INVALID_SOCKET, "the previous game still holds the tracker port");

    BioShockInfiniteHeadTracking::Config cfg;
    cfg.udp_port = port;

    BioShockInfiniteHeadTracking::TrackingRuntime runtime;
    runtime.Start(cfg);

    // Start must not report the contested port as a failure the caller has to act on:
    // it returns void precisely so nothing upstream can decide to give up here.
    Check(!runtime.IsReceiving(), "no tracking while the port is held");
    int wastedMs = 0;
    Check(!PumpUntilTracking(runtime, sender, port, 300, wastedMs),
          "frames render without tracking rather than stalling");

    // The player remembers, and closes the other game.
    closesocket(holder);

    int reclaimMs = 0;
    const bool reclaimed = PumpUntilTracking(runtime, sender, port, 5000, reclaimMs);
    Check(reclaimed, "tracking starts on its own once the port frees up");

    // The retry cadence is the whole guarantee: a supervisor that only tried once a
    // minute would still pass the check above and feel broken. Allow the retry
    // interval plus the supervisor's own tick and a frame or two of pumping.
    const int budgetMs = cameraunlock::UdpReceiver::kRetryIntervalMs * 3;
    Check(reclaimed && reclaimMs <= budgetMs, "and does so promptly, not eventually");
    std::printf("         reclaimed %dms after the port freed (budget %dms, retry interval %dms)\n",
                reclaimMs, budgetMs, cameraunlock::UdpReceiver::kRetryIntervalMs);

    runtime.Stop();

    // What the log SAYS about a refused bind, checked against what the OS actually
    // said about it. The message used to assert a cause it had never asked for -
    // "another app is listening on it -- OpenTrack, or another game" - which is only
    // one of the reasons a bind fails. A port inside a Hyper-V/WSL reserved range
    // refuses with WSAEACCES and no app is holding anything, and the user then spends
    // their evening closing trackers that were never the problem.
    //
    // The code the OS gives here is derived independently, by failing a bind of our
    // own against the same holder, so this compares the log against the OS rather than
    // against a constant typed out twice.
    SOCKET secondHolder = OpenHolder(port);
    Check(secondHolder != INVALID_SOCKET, "a holder is back on the port for the log check");

    SOCKET refused = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    // Checked, because the whole point of this block is that osError below is the code
    // the OS gave for THIS failure. An unchecked INVALID_SOCKET makes bind fail with
    // WSAENOTSOCK instead, refusedBind still passes, and the log comparison then fails
    // for a reason that has nothing to do with the log.
    Check(refused != INVALID_SOCKET, "a socket for the independent bind attempt");
    sockaddr_in refusedAddr = {};
    refusedAddr.sin_family = AF_INET;
    refusedAddr.sin_port = htons(port);
    refusedAddr.sin_addr.s_addr = INADDR_ANY;
    const bool refusedBind =
        bind(refused, reinterpret_cast<sockaddr*>(&refusedAddr), sizeof(refusedAddr)) == SOCKET_ERROR;
    const int osError = WSAGetLastError();
    closesocket(refused);
    Check(refusedBind, "the OS refuses a second bind on a held port");

    LogCapture capture;
    cameraunlock::UdpReceiver receiver;
    receiver.SetLog(capture.Sink());
    Check(!receiver.Start(port), "Start reports the contested port as unbound");

    Check(capture.AnyLineContains("error " + std::to_string(osError)),
          "the bind failure names the error code the OS gave");
    Check(!capture.AnyLineContains("another app is listening"),
          "and does not assert a cause the OS never stated");
    std::printf("         logged: %s\n", capture.First().c_str());

    closesocket(secondHolder);
    receiver.Stop();

    closesocket(sender);
    WSACleanup();

    if (g_failures == 0) {
        std::printf("Port reclaim tests: all passed\n");
        return 0;
    }
    std::printf("Port reclaim tests: %d FAILED\n", g_failures);
    return 1;
}
