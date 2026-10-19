#include "common/pad_script.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/log.h"

namespace padscript {

namespace {

// Opt-in self-test (SIMS2_AUTOSTART): a scripted pad on port 0, repeating a 10 s
// cycle from ~2 s after the first poll: Start (wButtons 0x0010) held 0-0.5 s,
// D-pad Up (wButtons 0x0001) held 3-3.5 s, then A (analog[0] = 0xFF, the Xbox
// pressure byte, not a wButtons bit) held 5-5.5 s, all released between. The
// intro/attract movies run for minutes and ignore early presses, so the cycle
// repeats: a Start lands whenever the title is responsive and an A whenever a
// menu is. Only ever ORs bits/bytes in, never clears a real press. Unset:
// no-op, pad untouched.
// Step mode (SIMS2_STEP=<path>): each new token written to that file is a burst
// of timed pulses generated here, on the game's own poll clock: NAME or NAMExN
// (START, A, B, X, Y, BACK, UP, DOWN, LEFT, RIGHT, LSUP, LSDOWN, LSLEFT,
// LSRIGHT, BLACK, WHITE, LT, RT, LTHUMB, RTHUMB; anything else = none), so a
// driver can do press -> screenshot -> decide -> press with an exact press
// count. Takes precedence over the cycle below.
bool StepPad(int port, input::Pad& pad) {
    static const char* path = std::getenv("SIMS2_STEP");
    if (!path || !path[0] || port != 0) return false;
    using namespace std::chrono;
    static std::string held;
    static int64_t last_read = 0, held_since = 0;
    const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    if (now - last_read >= 50) {
        last_read = now;
        std::string token;
        if (FILE* f = std::fopen(path, "r")) {
            char buf[32] = {};
            if (std::fgets(buf, sizeof buf, f)) token = buf;
            std::fclose(f);
        }
        while (!token.empty() && (token.back() == '\n' || token.back() == '\r' || token.back() == ' ')) token.pop_back();
        if (token != held) {
            held = token;
            held_since = now;
            LOGI("sims2 step: holding '%s' (port 0)", held.c_str());
        }
    }
    if (held.empty() || held == "NONE") return true;
    // Pulse train for the current token: NAMExN = N pulses (default 1), each
    // 130 ms on / 130 ms off for buttons, 160/160 for stick deflections (they
    // are polled against a threshold). After the burst the input rests, however
    // long the driver leaves the token in the file.
    std::string name = held;
    int count = 1;
    if (const size_t x = held.rfind('x'); x != std::string::npos && x + 1 < held.size() &&
        held.find_first_not_of("0123456789", x + 1) == std::string::npos) {
        count = std::atoi(held.c_str() + x + 1);
        name = held.substr(0, x);
        if (count < 1) count = 1;
        if (count > 40) count = 40;
    }
    const bool stick = name.starts_with("LS");
    const int64_t half = stick ? 160 : 130;
    const int64_t phase = (now - held_since) / half;
    if (phase >= 2 * count || (phase & 1)) return true;
    pad.connected = true;
    if (name == "START") pad.buttons = uint16_t(pad.buttons | 0x0010);
    else if (name == "BACK") pad.buttons = uint16_t(pad.buttons | 0x0020);
    else if (name == "UP") pad.buttons = uint16_t(pad.buttons | 0x0001);
    else if (name == "DOWN") pad.buttons = uint16_t(pad.buttons | 0x0002);
    else if (name == "LEFT") pad.buttons = uint16_t(pad.buttons | 0x0004);
    else if (name == "RIGHT") pad.buttons = uint16_t(pad.buttons | 0x0008);
    else if (name == "A") pad.analog[0] = 0xFF;
    else if (name == "B") pad.analog[1] = 0xFF;
    else if (name == "X") pad.analog[2] = 0xFF;
    else if (name == "Y") pad.analog[3] = 0xFF;
    else if (name == "LSUP") pad.ly = 30000;
    else if (name == "LSDOWN") pad.ly = -30000;
    else if (name == "LSLEFT") pad.lx = -30000;
    else if (name == "LSRIGHT") pad.lx = 30000;
    else if (name == "BLACK") pad.analog[4] = 0xFF;
    else if (name == "WHITE") pad.analog[5] = 0xFF;
    else if (name == "LT") pad.analog[6] = 0xFF;
    else if (name == "RT") pad.analog[7] = 0xFF;
    else if (name == "LTHUMB") pad.buttons = uint16_t(pad.buttons | 0x0040);
    else if (name == "RTHUMB") pad.buttons = uint16_t(pad.buttons | 0x0080);
    return true;
}

void AutostartPad(int port, input::Pad& pad) {
    if (StepPad(port, pad)) return;
    static const bool enabled = [] {
        const char* v = std::getenv("SIMS2_AUTOSTART");
        return v && v[0] && std::strcmp(v, "0") != 0;
    }();
    if (!enabled || port != 0) return;
    using namespace std::chrono;
    static std::atomic<int64_t> first_ms{0};
    static std::atomic<bool> start_down{false}, a_down{false}, up_down{false};
    const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    int64_t expected = 0;
    if (first_ms.compare_exchange_strong(expected, now)) {
        LOGI("sims2 autostart: enabled, first poll on port 0; scripted Start/A cycle from ~2 s");
        return;
    }
    const int64_t elapsed = now - first_ms.load(std::memory_order_relaxed);
    const int64_t phase = elapsed >= 2000 ? (elapsed - 2000) % 10000 : -1;
    const bool start = phase >= 0 && phase < 500;
    const bool up = phase >= 3000 && phase < 3500;
    const bool a = phase >= 5000 && phase < 5500;
    if (start) {
        pad.connected = true;
        pad.buttons = uint16_t(pad.buttons | 0x0010);  // Xbox Start
        if (!start_down.exchange(true))
            LOGI("sims2 autostart: synthetic Start pressed (port 0, wButtons=0x%04X)", pad.buttons);
    } else if (start_down.exchange(false)) {
        LOGI("sims2 autostart: synthetic Start released (port 0)");
    }
    if (up) {
        pad.connected = true;
        pad.buttons = uint16_t(pad.buttons | 0x0001);  // Xbox D-pad Up (e.g. No -> Yes)
        if (!up_down.exchange(true)) LOGI("sims2 autostart: synthetic DPadUp pressed (port 0, wButtons=0x%04X)", pad.buttons);
    } else if (up_down.exchange(false)) {
        LOGI("sims2 autostart: synthetic DPadUp released (port 0)");
    }
    if (a) {
        pad.connected = true;
        if (pad.analog[0] < 0xFF) pad.analog[0] = 0xFF;  // Xbox A, pressure byte
        if (!a_down.exchange(true)) LOGI("sims2 autostart: synthetic A pressed (port 0, analog[0]=0xFF)");
    } else if (a_down.exchange(false)) {
        LOGI("sims2 autostart: synthetic A released (port 0)");
    }
}

}  // namespace

void Apply(int port, input::Pad& pad) {
    AutostartPad(port, pad);
}

}  // namespace padscript
