// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
//
// Frame-rate / game-speed decoupling. Guest addresses (flat image, guest base 0x10000; see
// docs/analysis-frame-step.md, re-checked against build/sims2_flat.exe):
//
//   0x1C2400  sub_001C2400  the frame-step quantiser hooked below (only caller: 0x1C2520)
//   0x1C2520  sub_001C2520  the frame tick; NOT hooked, so it still does the clock sampling and
//                           restamp, ++0x3B8108, the fps readout and the subsystem notifications
//   0x3526D4  dword_3526D4  seconds-per-tick. Written only by the app ctor sub_001C2070
//                           (= dword_31642C = 1/60 s); read by sub_001C2400, by sub_001C2520
//                           (twice) and by the debug overlay sub_001D9FE0. No other writer.
//   0x339F40  n15           last frame's tick count, written by the quantiser. Other reader:
//                           sub_001D98C0, which forces it to 1 when smoothing is off (below).
//   0x3B810C  dword_3B810C  running tick total, written by the quantiser. Other readers: the pad
//                           polls sub_001BCA10 / sub_001C1690 snapshot it into 0x3B80F8, which
//                           itself has no readers (xrefs_to(0x3B80F8) is only those two writes).
//   0x339F44  flt_339F44    the one engine delta, written by sub_001C2520 (85 xrefs, 68
//                           functions). Its capped copies 0x3526D0 (<=1/30), 0x3526CC (<=0.05),
//                           0x3526C8 (<=1/15) and the fps readout 0x339F48 are all re-derived
//                           from it inside sub_001C2520, so fixing the quantiser fixes all five.
//   0x3B8108  dword_3B8108  frame counter, ++ once per frame in sub_001C2520. Its 40 xrefs were
//                           all read for this change; see the unverified block at the end.
//   0x3B8104  dword_3B8104  per-frame toggle beside it (readers sub_001D1380, sub_001D9890,
//                           sub_001D98C0: double-buffer / renderer forwarding; untouched).
//
// The engine measures real time: the clock at [0x339F38] returns rdtsc deltas over a fixed
// 733,333,333 Hz (sub_0021BECD / sub_0021BEDE) as elapsed seconds (sub_001B9C50). The defect is
// only the quantisation after it, exactly as in The Urbz (D:/Urbz/src/game/hooks.cpp).
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "cpu/x86_ops.h"
#include "xdk/hle.h"

// the frame step the engine is working with, seconds, published by sub_001C2400 below
namespace {
std::atomic<double> g_dt{1.0 / 60.0};

// Each wrapper can be switched off for the run, so a defect can be bisected against the stock
// behaviour without a rebuild: SIMS2_NO_STEP (the frame step itself), SIMS2_NO_REPEAT (pad
// auto-repeat counts). Off means the recompiled function runs exactly as the image wrote it.
bool HookOff(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] && std::strcmp(v, "0") != 0;
}
}  // namespace

// The frame step, sub_001C2400(eax = app object, float elapsed_seconds on the stack, retn 4),
// called once a frame from the frame tick sub_001C2520 at 0x1C259F.
//
// The engine measures real time (above), but then quantises it:
//
//     ticks = clamp((int)(elapsed / dword_3526D4 + 0.5), 1, 15)   // 0x3526D4 = 1/60 s
//     dt    = ticks * dword_3526D4                                // 0x1C2629, into [0x339F44]
//
// [0x339F44] is the one delta the rest of the engine multiplies by, so with a 1/60 s tick and a
// floor of one tick, every frame shorter than 1/60 s still advanced the game by a whole 1/60 s:
// at 144 fps the game runs 2.4x fast (144 * 1/60).
//
// The tick is only the engine's own unit, so the host picks a finer one and writes back a
// seconds-per-tick that makes `ticks * dword_3526D4` exactly the time that really passed. dt is
// then real seconds at any frame rate, and the engine's own 0.25 s ceiling (15 ticks of 1/60 s)
// is kept, so a long stall still cannot advance the world in one jump. dword_3526D4 is written
// before returning because the original caller reads it after this returns (0x1C2621-0x1C2629,
// and again at 0x1C25AD on the smoothing branch).
//
// Why changing the tick unit is safe, on this image's evidence:
// - dword_3526D4 has exactly one writer besides this hook (the ctor, above) and no reader that
//   treats the unit itself as meaningful, except the debug overlay sub_001D9FE0, which divides
//   by it: the value written here is always positive (dt >= 1 ms, ticks >= 1), and this hook
//   runs in the frame tick before any overlay draw, so it can never divide by zero.
// - n15 (0x339F40) has one other reader, sub_001D98C0, and when smoothing is off it forces the
//   forwarded value to 1 before handing it to the renderer object [0x3B81B4] vfunc +0x14:
//       if (*(BYTE *)(dword_3B8130 + 5) == 0) n15 = 1;
//   Smoothing is off by default (the ctor sub_001C2070 writes app+5 = 0, and no setter was
//   found), so the finer tick count (up to 250) never reaches the renderer on the shipping
//   path. If smoothing were on, sub_001C2520's dt branch would not use ticks * tick-unit at
//   all (it averages the sample history, 0x1C25AD-0x1C2631); that branch floors each sample
//   at dword_3526D4, so the hook's ~1 ms unit drops its floor from 1/60 s to ~1 ms - still
//   real-time, with its 15x cap then sitting at ~15 ms per sample.
// - dword_3B810C stays monotonic; only its unit changes (ms instead of 60ths). Its only other
//   readers snapshot it into 0x3B80F8, which nothing reads (above), so no duration measured in
//   the old unit can be misread.
// - The replacement deliberately does not consult the app+5 smoothing byte: sub_001C2520
//   passes the true elapsed at 0x1C2596 in both modes, and quantising that value is correct in
//   both (the smoothing caller's history is left for sub_001C2520 / sub_001C2750 to maintain).
X86_FUNC(__imp__sub_001C2400);
X86_FUNC(sub_001C2400) {
    if (HookOff("SIMS2_NO_STEP")) { __imp__sub_001C2400(ctx, base); return; }
    constexpr double kTick = 0.001;  // seconds per tick, 1/60 before this
    constexpr int kMaxTicks = 250;   // 0.25 s, the engine's own ceiling of 15 ticks of 1/60 s
    const float elapsed = X86_MEMF32(ctx.esp + 0x4u);
    // not (elapsed > 0): a NaN or a negative reading takes the smallest step, as the clamp did
    const double dt = elapsed > 0.0f ? std::min(double(elapsed), kMaxTicks * kTick) : kTick;
    const int ticks = std::clamp(int(dt / kTick + 0.5), 1, kMaxTicks);
    X86_MEMF32(0x3526D4u) = float(dt / ticks);  // ticks * this == the time that really passed
    X86_MEM32(0x339F40u) = uint32_t(ticks);     // n15
    X86_MEM32(0x3B810Cu) = X86_MEM32(0x3B810Cu) + uint32_t(ticks);
    ctx.eax = uint32_t(ticks);
    g_dt.store(dt, std::memory_order_relaxed);
    ctx.esp += 8;  // ret 4
}

// Button auto-repeat, sub_001BCA50(pad in eax, plain retn). Identified, not guessed - it is the
// same mechanism, at the same offsets, as the Urbz function of the same shape:
//
// - It counts each held button's byte timer at pad+0x1B8..pad+0x1D7 down by one per call; on
//   reaching zero it sets the button's bit in the repeat word at pad+0x1B4 and reloads the timer
//   from the rate byte at pad+0x1D9, and reloads from the delay byte at pad+0x1D8 whenever the
//   button is not held (0x1BCAAA-0x1BCB69, four buttons per loop pass, 32 buttons).
// - The stock delay/rate are set by the pad ctor sub_001BC820: [pad+0x1D8] = 10 at 0x1BC87D,
//   [pad+0x1D9] = 3 at 0x1BC884 - the same 10/3 as the Urbz pad.
// - It is called once per pad poll from both poll entry points (tail jump in sub_001BCA10 at
//   0x1BCA45, call in sub_001C1690 at 0x1C16C7, eax = the pad object in both), and the polls are
//   frame-driven: each first snapshots the tick total 0x3B810C into 0x3B80F8 when its flag at
//   pad+4 is clear (0x1BCA1A, 0x1C169D).
//
// Tied to calls, a 144 fps game therefore repeats 2.4x too fast. The counts are rescaled to the
// frame that really passed for the duration of the call and restored after: the scale is
// 1/(60 * dt), which does not depend on how many polls a frame makes, only on the polls being
// frame-driven - the console made the same number of calls per frame, so console timing is
// preserved at any frame rate. Only the two reload bytes are scaled; the timer bytes at
// pad+0x1B8 are the original's own state and keep the scaled reload they were given.
X86_FUNC(__imp__sub_001BCA50);
X86_FUNC(sub_001BCA50) {
    if (HookOff("SIMS2_NO_REPEAT")) { __imp__sub_001BCA50(ctx, base); return; }
    const uint32_t pad = ctx.eax;
    // [pad+0x1D8] is only a valid pad on the paths the polls take; anything else runs untouched
    // rather than writing two bytes into low guest memory.
    if (pad < 0x10000u) { __imp__sub_001BCA50(ctx, base); return; }
    const double scale = 1.0 / (60.0 * g_dt.load(std::memory_order_relaxed));
    const uint8_t delay = X86_MEM8(pad + 0x1D8u), rate = X86_MEM8(pad + 0x1D9u);
    X86_MEM8(pad + 0x1D8u) = uint8_t(std::clamp(int(delay * scale + 0.5), 1, 255));
    X86_MEM8(pad + 0x1D9u) = uint8_t(std::clamp(int(rate * scale + 0.5), 1, 255));
    __imp__sub_001BCA50(ctx, base);
    X86_MEM8(pad + 0x1D8u) = delay;
    X86_MEM8(pad + 0x1D9u) = rate;
}

// ---------------------------------------------------------------------------
// Unverified per-call consumers - investigated, deliberately NOT wrapped.
//
// The frame counter at 0x3B8108 has 40 xrefs. Every reader was read in the flat image; they
// fall into the groups below. None of them applies a constant rate per call in the way the
// wrapped pad repeat does, so none gets a speculative wrapper. If a live measurement (same
// scene, 60 fps vs uncapped) later shows one of these scaling with frame rate, wrap only that
// one, in the scale-the-constant-around-__imp__ style above.
//
// 1. Animation stamps and same-frame guards (the bulk; evidence says delta-driven):
//    - Passed in eax to sub_001D1060, which compares/stores it at [esi+0x30] as an
//      already-evaluated-this-frame guard (0x1D1063): call sites in sub_00025D00 (0x262AC),
//      sub_00027430 (0x27580), sub_00029360 (0x293E4), sub_0003A430 (0x3A47B),
//      sub_0003A8E0 (0x3A8FC), sub_00053080 (0x530A2), sub_00064300 (0x643E5),
//      sub_00067D60 (0x6801F), sub_00069290 (0x6932D), sub_00070C70 (0x71016),
//      sub_00071100 (0x71126), sub_00071350 (0x713D5), sub_00188060 (0x188135),
//      sub_001D1470 (0x1D147C), sub_001D4A60 (0x1D4A6F), sub_001D50D0 (0x1D50FD).
//      The advance that guard protects, sub_001D06D0, moves its cursor by
//      `rate * [edi+0x54] * [esi] * flt_339F44` (0x1D0882-0x1D0890) - multiplied by the delta,
//      so scaling it here would correct it twice.
//    - Stored as a node's start stamp at node+0x38 (with the cursor at node+0x40 reset from
//      (frames-1) * a constant, e.g. *0.0 at 0x316284): sub_001D3840 (0x1D3A64, frame-1),
//      sub_001D4340 (0x1D43D2), sub_0005DB40 (0x5DEBC, 0x5E4E1), sub_00061970 (0x61A7E),
//      sub_000620C0 (0x621AE), sub_00062420 (0x624C4), sub_000629A0 (0x62B64),
//      sub_00062EB0 (0x62F58), sub_00063B30 (0x63BD4), sub_00064160 (0x64283),
//      sub_00065420 (0x655ED, 0x65650), sub_00070C70 (0x7108B). sub_001D06D0 only compares
//      node+0x38 against the current frame for its same-frame snap (0x1D07E7-0x1D0802), and
//      sub_001D01F0 stores the frame at a1[10] the same way (0x1D01FE). Stamps, not rates.
//    - Once-per-frame guards proper: sub_00051B50 (0x51B50, against its cache at 0x3B7E8C;
//      its accumulation multiplies by flt_339F44 at 0x51B5B) and sub_001D1380 (0x1D1380,
//      stamp at a1[11], then selects a buffer via the 0x3B8104 toggle).
//
// 2. sub_00057550 (0x57591, 0x57612): the one genuine frame-count interval found. It caches a
//    boolean at this+0x3A0+a2 and re-polls only when the frame counter passes a stored
//    deadline, resetting it to `frame + (rand() & 0xF) + 10` - every 10-25 frames. What the
//    poll decides (vfunc +0x12C on [this+0x318], reached only through a pointer table at
//    0x2F6744+) was not identified, and there is no global constant to scale: the interval is
//    computed from the frame counter itself, so the scale-around-__imp__ pattern cannot
//    express a fix. Left alone until a measurement names its effect.
//
// 3. sub_00042050 (0x420AC, 0x42105): uses `frame & 7` only to seed its round-robin slot
//    search over dword_322F3C[0..7]; its countdown at this+0x100 is decremented by its
//    caller-supplied float argument and reset to 4.0 (dword_3162E8). The frame value is an
//    index seed, not a rate.
//
// 4. The Urbz port's other per-call consumers - camera zoom rate, menu held-repeat gate,
//    idle auto-return easing, shot-mode glide (D:/Urbz/src/game/hooks.cpp) - have no Sims 2
//    counterpart identified in this static pass. The Sims camera and UI are different code,
//    no function was found applying a constant per-call rate without a flt_339F44 factor
//    outside group 2, and menu held-repeat in this image goes through the pad repeat word
//    at pad+0x1B4 produced by the wrapped sub_001BCA50 above. Wrapping any of these by
//    analogy would be inventing a consumer; measure first (units/second at two caps, same
//    scene) and wrap only what measurably scales with frame rate.
