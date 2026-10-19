# Frame step: how The Sims 2 (Xbox) advances its simulation clock

Static analysis of `build/sims2_flat.exe` (guest base `0x10000`, IDA database
`0df7ac79bd00ebc1-sims2_flat.exe`). The native build was not run; every claim below is
either a guest address with a disassembly/pseudocode excerpt, or a `file:line` in
`D:/Urbz`. Anything not confirmed that way is marked UNCONFIRMED.

**Answer in one paragraph:** the engine measures real elapsed seconds with an
`rdtsc`-backed performance counter, then throws the precision away in exactly the same
way The Urbz did. `sub_001C2400` quantises the elapsed time to a whole number of
1/60 s ticks (clamped 1..15), and `sub_001C2520` publishes
`dt = ticks * 1/60` into the single delta global `flt_339F44` at `0x339F44`
(85 xrefs in 68 functions). At any frame rate above 60 the game therefore runs fast —
at 144 fps, 2.4× — and the fix is the Urbz fix transplanted: replace only the
quantiser `sub_001C2400` in `src/game/hooks.cpp` with a 1 ms quantiser that writes
back a seconds-per-tick of `dt / ticks` into `dword_3526D4` at `0x3526D4`, so the
original `ticks * dword_3526D4` multiplication in `sub_001C2520` yields the real
elapsed time. Details, evidence and caveats below.

---

## 1. What the sibling port replaced (reference)

Read first, per the assignment: `D:/Urbz/src/game/hooks.cpp`,
`D:/Urbz/docs/status.md`, `D:/Urbz/config/game.toml`.

The Urbz frame clock (same engine family, same XDK 5849):

- The clock singleton at `[0x358914]` reads `rdtsc` through XAPI
  `QueryPerformanceCounter` (`sub_001F0DDA`) over a fixed 733,333,333 Hz
  (`[0x2F0200]`), so elapsed seconds are real. Vblank plays no part
  (`skill://urbz-frame-rate-and-game-speed`; `D:/Urbz/docs/status.md:273-275`).
- Frame tick `sub_0018E480` (once per frame from `sub_0018EED0`) calls
  `sub_0018E360(float elapsed_seconds)`, which did
  `ticks = clamp((int)(elapsed / [0x2F024C] + 0.5), 1, 15)` with
  `[0x2F024C] = 1/60 s`, then `dt = ticks * [0x2F024C]` at `0x18E580` into
  **`[0x2E2A80]`**, the engine's one delta (97 reads in 63 functions)
  (`D:/Urbz/src/game/hooks.cpp:45-68`, `D:/Urbz/docs/status.md:275-285`).
- The host replacement is a strong `X86_FUNC(sub_0018E360)` that picks 1 ms ticks
  (max 250 = the engine's own 0.25 s ceiling), writes
  `X86_MEMF32(0x2F024C) = dt / ticks`, the tick count to `[0x2E2A7C]`, accumulates
  `[0x358898]`, returns ticks in `eax`, and pops the one stack argument with
  `ctx.esp += 8` (`ret 4`). An env kill-switch (`URBZ_NO_STEP`) falls back to
  `__imp__sub_0018E360` (`D:/Urbz/src/game/hooks.cpp:69-84`). Verified 1.000 game
  seconds per real second at 60 / 144 / unlimited
  (`D:/Urbz/docs/status.md:314-316`).
- Why changing the tick unit is safe there: the tick count `[0x2E2A7C]` has one
  other reader on a never-enabled smoothing path, and the tick total `[0x358898]`
  is copied somewhere never read (`D:/Urbz/src/game/hooks.cpp:64-68`).

The Sims 2 image contains the same design, at different addresses, with one extra
feature (an optional 3-tap smoothing path, off by default). The rest of this
document is the Sims 2 evidence.

---

## 2. Ranked candidates

### Candidate 1 — `sub_001C2400`: the quantiser. Confidence: HIGH (this is the function to hook)

Guest `0x1C2400`, called only from `sub_001C2520` at `0x1C259F`. Signature from the
disassembly: `this` in `eax`, one float stack argument (elapsed seconds), `retn 4`,
returns the tick count in `eax`. Full disassembly (`build/sims2_flat.exe:0x1C2400:asm`):

```asm
0x1c2400  mov     cl, [eax+5]              ; smoothing flag on the app object
0x1c2403  test    cl, cl
0x1c2405  jz      loc_1C248B               ; -> non-smoothing path
   ... smoothing branch: clamp the sampled history (see §4) ...
0x1c244c  fld     [esp+arg_0]
0x1c2450  fdiv    dword_3526D4             ; elapsed / seconds-per-tick
0x1c2456  fadd    dbl_3162D8               ; + 0.5  (round to nearest)
0x1c245c  call    _sub_0025A514            ; (int) truncation (CRT _ftol)
0x1c2461  cmp     eax, 1
0x1c2464  mov     n15, eax                 ; n15 = [0x339F40], the tick count
0x1c246b  cmp     eax, 0Fh
0x1c246e  jle     short loc_1C247A
0x1c2470  mov     eax, 0Fh                 ; clamp high: 15 ticks
   ...
0x1c247a  mov     ecx, dword_3B810C
0x1c2480  add     ecx, eax
0x1c2482  mov     dword_3B810C, ecx        ; tick total += ticks
0x1c2488  retn    4
0x1c248b  fld     [esp+arg_0]              ; non-smoothing path: same arithmetic
0x1c248f  fdiv    dword_3526D4             ;   on the passed-in elapsed
0x1c2495  fadd    dbl_3162D8
0x1c249b  call    _sub_0025A514
0x1c24a0  cmp     eax, 1                   ; clamp low: 1 tick (0x1C24AA)
   ... clamp high: 15 ticks (0x1C24BC) ...
0x1c24ce  retn    4
```

Constants, read out of the image with IDA (`ida_bytes`):

| Address | Value | Role |
| --- | --- | --- |
| `dword_31642C` (`0x31642C`, .rdata) | `0x3C888889` = 1/60 s | stock tick size, copied into the writable tick size at construction |
| `dword_3526D4` (`0x3526D4`, .data) | 1/60 s at runtime (0 in the file; written by the ctor) | **seconds-per-tick**, the divisor above and the multiplier in Candidate 2 |
| `dbl_3162D8` (`0x3162D8`) | `0.5` (double) | rounding term |
| `n15` (`0x339F40`) | — | last frame's tick count |
| `dword_3B810C` (`0x3B810C`) | — | running tick total |

So: `ticks = clamp((int)(elapsed / dword_3526D4 + 0.5), 1, 15)`. This is
instruction-for-instruction the Urbz `sub_0018E360` computation
(`ticks = clamp((int)(elapsed / [0x2F024C] + 0.5), 1, 15)`).

Predicted defect, from the arithmetic alone: at 144 fps the real elapsed is
6.94 ms, `(int)(0.00694 / 0.016667 + 0.5) = (int)(0.916) = 0`, clamped up to 1
tick, so the frame advances a whole 1/60 s: 144 × 1/60 = **2.4 game seconds per
real second**. At any rate below 60 the step is honest (rounded to the nearest
60th, capped at 15 ticks = 0.25 s).

### Candidate 2 — `sub_001C2520`: the frame tick that publishes the delta. Confidence: HIGH (same system; do not hook this one, see §5)

Guest `0x1C2520`, `__thiscall` on the app object (singleton pointer at
`[0x3B8130]`). It is vtable slot `+0x18` of the app class: base vtable
`0x3137A8` slot 6 = `0x1C2520`; derived vtable `0x313F78` slot 6 = thunk
`sub_001DB000` → `sub_001C2520`. What it does, in order
(`build/sims2_flat.exe:0x1C2520:asm`):

1. Samples the clock (below, Candidate 4): counter into `app+0x10` at
   `0x1C2547` (`call [eax+0Ch]`), elapsed seconds into the x87 stack at
   `0x1C2575` (`call [edx+10h]`, vs. the stored stamp at `app+8`), restamps
   `app+8` at `0x1C2585`, accumulates the total elapsed into `dbl_3B8118`.
2. `++dword_3B8108` (frame counter) and toggles `dword_3B8104`
   (`0x1C254A-0x1C2570`).
3. Calls the quantiser: `push elapsed; mov eax, esi; call _sub_001C2400` at
   `0x1C2596-0x1C259F`.
4. Computes the delta. Non-smoothing path (`app+5 == 0`):
   ```asm
   0x1c2621  cvtsi2ss xmm0, n15          ; (float)ticks
   0x1c2629  mulss    xmm0, dword_3526D4 ; dt = ticks * seconds-per-tick
   ```
   Smoothing path: 3-tap average of clamped elapsed samples, §4.
5. Publishes the delta and its capped variants:
   ```asm
   0x1c263c  movss   flt_339F44, xmm0    ; [0x339F44] = dt  (the engine delta)
   0x1c2644  movss   dword_3526D0, xmm0  ; capped copy...
   0x1c264e  movss   dword_3526D0, xmm1  ;   ...at dword_316324 = 1/30 s
   0x1c2661  movss   dword_3526CC, xmm0  ; capped copy at dword_316368 = 0.05 s
   0x1c267e  movss   dword_3526C8, xmm0  ; capped copy at dword_2F67B8 = 1/15 s
   0x1c26a4  divss   xmm1, xmm0          ; 1.0 (dword_316248) / dt
   0x1c26b5  movss   dword_339F48, xmm1  ; [0x339F48] = fps
   ```
6. Tail: a per-frame timer accumulator (`dword_3EB2A8 += flt_339F44` at
   `0x1C2707-0x1C271A`, firing `sub_001F9700` past a threshold) and two
   subsystem notifications (`[0x352B84]` vfunc `+0x14`, tail-jump to
   `[0x352B88]` vfunc `+0x14` at `0x1C2736-0x1C274C`). These side effects are
   why the hook belongs in the quantiser, not here.

**The delta global is `flt_339F44` at `0x339F44`.** IDA `xrefs_to(0x339F44)`:
85 references in 68 functions across the engine (`.text` gameplay, animation
and UI addresses; the writer is `sub_001C2520`). For comparison, the Urbz
delta `[0x2E2A80]` had 97 reads in 63 functions. The capped variants have their
own consumers (`dword_3526CC`: `sub_0007CDF0`, `sub_0007CFC0`;
`dword_3526C8`: `sub_00028F10`, `sub_0007B360`, `sub_001C1550`,
`sub_001C15D0`, `sub_001C1EF0`), all fed from the same `dt`, so fixing the
quantiser fixes all four globals at once. UNCONFIRMED: which gameplay system
each capped variant feeds (the `0x1C1xxx` cluster sits in the same code region
as the clock class; naming them needs runtime or deeper tracing).

**Caller (once per frame):** `sub_001BD700` at `0x1BD854`:
```asm
0x1bd846  mov     ecx, dword_3B8130   ; the app singleton
0x1bd852  mov     eax, [ecx]
0x1bd854  call    dword ptr [eax+18h] ; -> vtable +0x18 = sub_001C2520 (frame tick)
0x1bd857  call    _sub_001F7E70
   ...
0x1bd87b  mov     ecx, dword_3B8130
0x1bd883  call    dword ptr [edx+1Ch] ; -> vtable +0x1C = sub_001C2750 (elapsed sampler, §4)
```
`sub_001BD700` is itself a virtual (only OFFSET xrefs), a per-frame state
update on the game thread. Above it, the static entry chain is: XBE entry
`0x21BF66` → thread proc `sub_0021BEF2` → `sub_001F34F0(0,0)` at `0x21BF66`'s
callee list → init `sub_001F3390` / `sub_001F3490` (the latter calls app
vfuncs on `[0x3B8130]`). The exact main-loop function that dispatches
`sub_001BD700` each frame is UNCONFIRMED (virtual dispatch; not needed for
the fix).

The app object: constructed by `sub_001C2070` (base vtable `0x3137A8`,
sets `app+4 = 0`, `app+5 = 0` — smoothing off — `app+6 = 1`, and
`dword_3526D4 = dword_31642C` = 1/60), wrapped by `sub_001DAB50` which
installs the derived vtable `0x313F78` and stores the singleton:
`*a1 = &off_313F78; dword_3B8130 = (int)a1;`.

### Candidate 3 — `sub_001C2750`: the smoothing sampler. Confidence: HIGH that it is part of the same clock; MEDIUM relevance (path off by default)

Guest `0x1C2750`, vtable slot `+0x1C`, called at `0x1BD883` right after the
frame tick. It refreshes the sample history used by the smoothing branch:

```c
// _sub_001C2750 @ 0x1c2750-0x1c27a2
*(float *)&qword_339F50     = elapsed_seconds(clock, this + 16);
*(float *)((char *)&qword_339F50 + 4) = previous sample;   // history shift
```

i.e. `0x339F50/0x339F54/0x339F5C/0x339F60` hold recent elapsed samples
(all initialised to 1/60 in the image). Only relevant if smoothing is on; §4.

### Candidate 4 — the time source: `sub_001B9C50` / `sub_0021BECD` / `sub_0021BEDE`. Confidence: HIGH (supporting evidence, nothing to hook)

The frame tick does not read any XDK tick API directly; it calls a clock
object whose pointer lives at `off_339F38` (`[0x339F38]` → instance
`0x3CEAE8`, vtable `0x313258`, ctor `sub_001B9BE0`, registered by
`sub_0026EAF0`/`sub_002704B0`):

- vtable `+0x0C` = `sub_001B9C40` → thunk to `sub_0021BECD`:
  ```c
  // _sub_0021BECD @ 0x21becd-0x21bede  (XAPILIB QueryPerformanceCounter)
  int __stdcall sub_0021BECD(_QWORD *a1) { *a1 = __rdtsc(); return 1; }
  ```
- vtable `+0x10` = `sub_001B9C50`, the elapsed-seconds method the tick calls:
  ```c
  // _sub_001B9C50 @ 0x1b9c50-0x1b9c8e
  double __stdcall sub_001B9C50(_QWORD *a1) {
    sub_0021BECD(&v2);
    return (double)(v2 - *a1) / (double)qword_352680;
  }
  ```
- `qword_352680` is filled once by `sub_0021BEDE`
  (XAPILIB `QueryPerformanceFrequency`): `*a1 = 733333333; a1[1] = 0; return 1;`
  — the constant `733333333` sits at `0x21BEE8` in that function.

So the measured elapsed is real seconds (`rdtsc` delta / 733,333,333 Hz),
identical in design to the Urbz clock
(`D:/Urbz/docs/status.md:273-275`). On the host this resolves through the
SDK's XAPI/rdtsc handling the same way; the measurement is not the problem,
the quantisation after it is.

Note on the assignment's XDK leads: `KeQueryPerformanceCounter`,
`KeQueryPerformanceFrequency`, `GetTickCount`, `QueryPerformanceCounter`,
`timeGetTime`, `KeTickCount` do not appear in `[symbols]` in
`config/game.toml` at all (that table holds D3D/DSOUND/XAPI names; grep for
`Performance|TickCount|timeGetTime` in `config/game.toml` finds nothing),
and the flat PE has 0 imports — the game reaches time through the statically
linked XAPILIB routines above, not through kernel thunks. That lead is
therefore closed with evidence, and the two D3D leads resolve as
presentation-only (Candidate 5).

### Candidate 5 — the present path: `D3DDevice_Swap` callers and `D3DDevice_BlockUntilVerticalBlank`. Confidence: HIGH that these are NOT the simulation step

- `D3DDevice_Swap` (guest `0x274DD0`, `config/game.toml` `[symbols]`) is
  called from `_sub_001F7EE0` (the dirty-disc error screen, `__noreturn`,
  swaps around drawing the "There's a problem with the disc" text), and from
  the render-thread command handler `_sub_001FC550` (command type 4:
  `SetRenderTarget`, fence, `D3DDevice_Swap(0)` at its tail) dispatched by
  `sub_001FC0E0`. That is a renderer worker thread consuming queued
  commands, not the game loop.
- `D3DDevice_BlockUntilVerticalBlank` (guest `0x2712C0`) is called from
  `sub_001FC0E0` command type 6 (vblank wait / display-mode switch on the
  render thread) and from D3D-internal `sub_002743D0`. It gates
  *presentation* on the render thread. On the host, presentation rate is
  set by the SDK's swap pacing (`app::Pace`, vsync, `fps_cap` — compare
  `D:/Urbz/docs/status.md:269-271`, where the pacer was measured holding
  60 and 144 swaps/s while the game still ran fast). Blocking there cannot
  fix game speed; it can only cap the frame rate.

Conclusion of the ranking: the simulation clock is Candidates 1+2+4 as one
system; Candidate 3 is its optional smoothing branch; Candidate 5 is what
gates the *frame rate* on the host, while Candidates 1+2 are what gate the
*game speed*.

---

## 3. The best candidate: exact computation, globals, callers

Function: **`sub_001C2400` (quantiser), driven by `sub_001C2520` (frame tick).**

Computation, assembled from the two disassemblies:

```text
elapsed  = (rdtsc_now - rdtsc_at_app+8) / 733333333.0        ; sub_001B9C50, real seconds
ticks    = clamp((int)(elapsed / dword_3526D4 + 0.5), 1, 15) ; sub_001C2400
n15 [0x339F40]        = ticks
dword_3B810C         += ticks                                 ; tick total
dt       = smoothing_on(app+5) ? avg3(clamped elapsed samples) ; sub_001C2520
                               : (float)ticks * dword_3526D4
flt_339F44 [0x339F44] = dt                                    ; THE delta, 85 xrefs / 68 funcs
dword_3526D0          = min(dt, 1/30)                         ; 0x316324
dword_3526CC          = min(dt, 0.05)                         ; 0x316368
dword_3526C8          = min(dt, 1/15)                         ; 0x2F67B8
dword_339F48          = (dt != 0) ? 1.0 / dt : 0              ; fps readout
dword_3B8108++ ; dword_3B8104 ^= 1                            ; frame counter / toggle
```

Globals written by the pair, and who else touches them (IDA `xrefs_to`):

| Global | Written by | Other readers (evidence) |
| --- | --- | --- |
| `flt_339F44` `0x339F44` (dt) | `sub_001C2520` | 85 xrefs, 68 functions engine-wide |
| `dword_3526D4` `0x3526D4` (s/tick) | ctor `sub_001C2070` (= 1/60) | `sub_001C2400`, `sub_001C2520`, debug overlay `sub_001D9FE0` — no other writer |
| `n15` `0x339F40` (ticks) | `sub_001C2400`, `sub_001C2520` | `sub_001D98C0` (see §6) |
| `dword_3B810C` (tick total) | `sub_001C2400` | pad polls `sub_001BCA10`, `sub_001C1690` (snapshot into `dword_3B80F8`) |
| `dword_3B8108` (frame count) | `sub_001C2520` | 40 xrefs (see §6) |
| `dword_339F48` (fps) | `sub_001C2520` | debug overlay `sub_001D9FE0` prints the clock fields |

Callers: `sub_001C2400` ← `sub_001C2520` at `0x1C259F` (only caller).
`sub_001C2520` ← thunk `sub_001DB000`, ← app vtable `0x313F78+0x18`,
← `sub_001BD700` at `0x1BD854`, once per frame update.

---

## 4. The second branch: smoothing (off by default)

If byte `app+5` is set, the quantiser ignores its stack argument and derives
ticks from the sample history instead (median-of-three-style clamp of
`0x339F50..0x339F60`, asm `0x1C240B-0x1C244C`), and `sub_001C2520` computes
`dt` as a 3-tap moving average, `0x1C25AD-0x1C2631`:

```text
sample = clamp(elapsed, dword_3526D4, 15 * dword_3526D4)   ; flt_3163C8 = 15.0
dt     = (history[1] + history[0] + sample) * (1/3)        ; dword_316438 = 0x3EAAAAAB
```

Evidence it is off in practice: the ctor `sub_001C2070` writes
`*(BYTE *)(a1 + 5) = 0` (and `app+6 = 1`); no setter was found among the app
vtable slots checked (`sub_001C2290` only clears the singleton pointer,
`sub_001C22A0` is an init that restamps the clock, neither sets `+5`).
UNCONFIRMED that nothing anywhere sets `app+5` at runtime — a byte write
through a pointer would not show up in the vtable walk. Two consequences
for the host plan: (a) the default path is the quantised one, so the bug
and the fix are as described; (b) even if smoothing were on, its `dt`
branch floors every sample at `dword_3526D4` (one tick), so at 144 fps it
would still advance ≈1/60 s per frame — the hook in §5 keeps this safe
because it rewrites `dword_3526D4` to ≈1 ms, dropping that floor to 1 ms
(the branch's 15× cap then sits at ≈15 ms per sample, a slight clamp of
the averaged sample on >66 ms frames; acceptable, and worth a live check
if smoothing is ever observed on).

---

## 5. Host-side plan (`D:/sims2/src/game/hooks.cpp`)

Current state of the file: a 12-line stub (`src/game/hooks.cpp:1-12`,
"No overrides are known for this title yet"). Add **one** override,
transplanted from `D:/Urbz/src/game/hooks.cpp:69-84` with the Sims 2
addresses, plus the `g_dt` atomic and `HookOff` helper copied from
`D:/Urbz/src/game/hooks.cpp:15-27` (rename the kill-switch `SIMS2_NO_STEP`).

Hook the quantiser only — not `sub_001C2520` — so the original still does
the clock sampling/restamping, the frame counter, all four delta globals,
the fps readout, the per-frame timer and the two subsystem notifications:

```cpp
// The frame step, sub_001C2400(eax = app object, float elapsed_seconds on the
// stack, retn 4), called once a frame from the frame tick sub_001C2520 at
// 0x1C259F. The engine measures real time (rdtsc through XAPILIB
// QueryPerformanceCounter sub_0021BECD over a fixed 733,333,333 Hz, elapsed
// from sub_001B9C50), but then quantises it:
//     ticks = clamp((int)(elapsed / dword_3526D4 + 0.5), 1, 15)  // 0x3526D4 = 1/60 s
//     dt    = ticks * dword_3526D4                               // 0x1C2629, into [0x339F44]
// [0x339F44] is the one delta the rest of the engine multiplies by (85 xrefs in
// 68 functions), so every frame shorter than 25 ms still advanced the game by
// a whole 1/60 s: at 144 fps the game runs 2.4x fast. Same defect and same
// fix as The Urbz (src/game/hooks.cpp there, sub_0018E360).
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
```

Why each line is safe, on Sims 2 evidence (mirroring the Urbz reasoning at
`D:/Urbz/src/game/hooks.cpp:59-68`):

- `dword_3526D4` has exactly one writer in the image (the ctor, setting
  1/60) and two engine readers besides the debug overlay
  (`xrefs_to(0x3526D4)`: `sub_001C2070`, `sub_001C2400`, `sub_001C2520`,
  `sub_001D9FE0`). Writing `dt / ticks` there each frame is the whole trick:
  `sub_001C2520` then computes `(float)n15 * dword_3526D4 == dt`, and the
  three capped globals and the fps readout follow from that same value.
- The calling convention matches the disassembly: stack float at
  `[esp+4]`, result in `eax`, `retn 4` emulated with `ctx.esp += 8`
  (return address + one dword arg) — the same shape as the Urbz hook
  (`ctx.esp += 8  // ret 4`, `D:/Urbz/src/game/hooks.cpp:83`). `this` in
  `eax` is only read for the smoothing byte, which the replacement
  deliberately does not consult: it quantises the passed-in elapsed in
  both modes (`sub_001C2520` passes the true elapsed at `0x1C2596`
  regardless of mode), and §4 covers the residual smoothing-branch floor.
- The 0.25 s ceiling is preserved: stock is 15 ticks × 1/60 s; 250 ticks ×
  1 ms is the same ceiling, so a long stall still cannot advance the world
  in one jump (compare `D:/Urbz/src/game/hooks.cpp:61-62`).
- `__imp__sub_001C2400` remains available and is used verbatim when
  `SIMS2_NO_STEP` is set, so the stock behaviour can be bisected without a
  rebuild (the Urbz pattern, `D:/Urbz/src/game/hooks.cpp:71`).

Rejected alternative: overriding `sub_001C2520` wholesale and writing
`flt_339F44` directly. It would also have to reproduce the clock restamp
sequence (`0x1C2530-0x1C2599`), `dword_3B8108`/`dword_3B8104`, the capped
globals, the fps global, the `dword_3EB2A8` timer and the `[0x352B84]` /
`[0x352B88]` notifications (`0x1C26E8-0x1C274C`) bit-exactly, for no benefit:
the quantiser hook already makes every one of those outputs correct
because they all derive from `ticks * dword_3526D4`.

If a future defect points at the smoothing branch (§4) being enabled, the
second override to add — only then — is `X86_FUNC(sub_001C2520)` wrapping
`__imp__sub_001C2520` while temporarily setting `dword_3526D4` to the
1 ms-derived value around the call; do not write it speculatively.

### Verification (for whoever runs the build; this analysis is static)

1. Throwaway probe inside the hook (the Urbz method,
   `skill://urbz-frame-rate-and-game-speed`, "Proving a change"): sum the
   published `dt` (`g_dt`) against `std::chrono::steady_clock` for one
   second and log the ratio. Correct is **1.000 game s per real s** at
   `fps_cap` 60, 144 and 0 (unlimited). Remove the probe before shipping.
2. Read `[0x339F48]` (the engine's own fps global) live: it should track
   the cap, while the in-game clock advances at the same game-time rate
   at every cap (Urbz cross-check shape: a fixed game-minutes-per-wall-
   second figure, `D:/Urbz/docs/status.md:314-316`; the Sims 2 ratio
   itself is UNCONFIRMED until measured).
3. Spot-check one delta consumer at 60 vs uncapped in the same scene
   (Sim walk speed, or an animation loop): units/second must match; a
   ratio near (uncapped fps / 60) is the signature of a remaining
   per-call consumer.
4. Regression checks specific to this hook: the debug clock overlay path
   (`sub_001D9FE0`, divides samples by `dword_3526D4`) must not divide by
   zero on the first frame (the hook always writes a positive tick size
   before any overlay draw, since the tick runs first at `0x1BD854`), and
   pad polling (`sub_001BCA10`/`sub_001C1690` snapshot `dword_3B810C`)
   must still see a monotonically increasing tick total — it does, the
   hook accumulates it identically, only in finer units.

---

## 6. Second speed dependencies a naive change could break

Searched for, with results:

1. **Tick-count forwarding to the renderer — benign, and it explains why
   the tick unit is free.** `sub_001D98C0` reads `n15` (`0x339F40`) and
   passes it, with the frame toggle `dword_3B8104`, to the renderer object
   `[0x3B81B4]` vfunc `+0x14`; but when smoothing is off (the default) it
   forces the value to 1 first:
   ```c
   n15 = ::n15;
   if ( *(_BYTE *)(dword_3B8130 + 5) == 0 )
     n15 = 1;
   return (*(int (__thiscall **)(int, int, int))(*(_DWORD *)dword_3B81B4 + 20))(
              a1: dword_3B81B4, a2: dword_3B8104, a3: n15);
   ```
   So on the shipping path the renderer receives a constant 1, and the
   hook's finer ticks (up to 250) never reach it. This mirrors the Urbz
   finding that the forwarded tick field is discarded by the render
   thread (`D:/Urbz/src/game/hooks.cpp:64-68`). UNCONFIRMED: the receiving
   vfunc's use of the argument (render-thread object `[0x3B81B4]`, whose
   present handler is `sub_001FC550`); check once at runtime if render
   timing ever looks wrong after the hook.
2. **Tick total as a timestamp — benign.** `dword_3B810C` is snapshotted
   into `dword_3B80F8` by both pad-poll entry points (`sub_001BCA10`,
   `sub_001C1690`: `if (*(this + 1) == 0) dword_3B80F8 = dword_3B810C;`).
   It stays monotonic under the hook; only its unit changes (ms instead
   of 60ths). Any code measuring *durations* in that unit would misread —
   no such reader was found (`xrefs_to(0x3B810C)`: only those two polls
   plus the quantiser), the same shape as Urbz's copied-never-read total.
3. **Frame counter `dword_3B8108` — the one open risk.** 40 xrefs across
   gameplay functions. Code that does something "every N frames" instead
   of accumulating `flt_339F44` will speed up linearly with frame rate
   regardless of this hook — the Urbz port found exactly such per-call
   consumers after fixing its step (camera zoom `sub_0001FC20`, pad
   auto-repeat `sub_0018D6D0`, menu repeat gate, idle-camera acceleration,
   shot glide; `D:/Urbz/src/game/hooks.cpp:86-286`) and proved the rest
   delta-driven by measuring units/second at two caps. No per-call
   consumer is *identified* in the Sims 2 image by this static pass, and
   none should be invented: after the step hook lands, repeat the Urbz
   measurement (60 vs uncapped, same scene) for Sim movement, camera,
   menu repeat and animation, and wrap only what measurably scales with
   fps, using the same scale-the-constant-around-`__imp__` pattern. The
   delta reader list (68 functions) notably contains **no** function in
   the DSOUND section (`0x2A5740+`) or XMV section (`0x2AF920+`), so audio
   and movie playback do not consume the simulation delta; movie pacing
   on this engine family is vblank+audio driven (Urbz,
   `D:/Urbz/docs/status.md:310-312`) — for Sims 2 specifically that is
   UNCONFIRMED, but if true it is unaffected by the step hook either way.
4. **Capped delta variants — handled, do not "fix" separately.**
   `dword_3526C8` (≤1/15 s), `dword_3526CC` (≤0.05 s) and `dword_3526D0`
   (≤1/30 s) are re-derived from the new `dt` inside `sub_001C2520` on
   every frame, so subsystems using a capped step (plausibly animation,
   by their `0x1C1xxx`/`0x7Bxxx` reader addresses — UNCONFIRMED) keep
   their caps exactly. At high fps they simply stop hitting the cap,
   which is the correct real-time behaviour.
5. **Animation / physics substeps keyed to ticks — none found.** The only
   place ticks are converted back to time is
   `(float)n15 * dword_3526D4` at `0x1C2629`. There is no fixed-step
   accumulator of the form "run physics N times per frame" in the tick
   path: one call to `sub_001C2520` produces one `dt`, however large
   (≤0.25 s), and consumers integrate with it. UNCONFIRMED beyond the
   tick path itself (a consumer could internally substep on `dt`; that
   would still be real-time correct under the hook).

**What gates the frame rate, then?** Not the simulation step. Frames are
paced by presentation: the render thread's present (`sub_001FC550` →
`D3DDevice_Swap` `0x274DD0`) and vblank wait (command type 6 in
`sub_001FC0E0` → `D3DDevice_BlockUntilVerticalBlank` `0x2712C0`), which on
the host are governed by the SDK's swap pacing (`fps_cap`, vsync). Set
the cap to 144 or 0 with the hook in place and the game renders at that
rate while advancing 1.000 game second per real second; without the hook
it advances `fps/60` game seconds per real second.

---

## Appendix: evidence index

| Claim | Evidence |
| --- | --- |
| Entry chain | `0x21BF66` pseudocode (callees `sub_0021D02B`, `sub_0021C6A2`, `sub_0021BBD8`); thread proc `sub_0021BEF2` calls `sub_001F34F0(0,0)` |
| Quantiser arithmetic | `build/sims2_flat.exe:0x1C2400:asm` (`0x1C2450` `fdiv dword_3526D4`, `0x1C2456` `fadd dbl_3162D8`, clamps at `0x1C246B`/`0x1C24AA`) |
| Tick size = 1/60 | `dword_31642C = 0x3C888889` read from image; ctor `sub_001C2070`: `dword_3526D4 = v5` where `v5 = dword_31642C` |
| `dt = ticks × tick` | asm `0x1C2621-0x1C2629` in `sub_001C2520` |
| Delta global + caps + fps | asm `0x1C263C-0x1C26B5`; constants `0x316324`=1/30, `0x316368`=0.05, `0x2F67B8`=`0x3D888889`=1/15, `0x316248`=1.0 read from image |
| Delta readership | IDA `xrefs_to(0x339F44)`: 85 refs, 68 functions |
| Once-per-frame call site | asm `0x1BD846-0x1BD883` in `sub_001BD700` (`call [eax+18h]`, `call [edx+1Ch]` on `[0x3B8130]`) |
| App vtable slots | vtable dumps: `0x3137A8`[6]=`0x1C2520`, `0x313F78`[6]=`sub_001DB000` thunk → `sub_001C2520`, [7]=`0x1C2750` |
| Real-time source | `sub_0021BECD` (`*a1 = __rdtsc()`), `sub_0021BEDE` (`*a1 = 733333333`, const at `0x21BEE8`), `sub_001B9C50` (counter delta / `qword_352680`), clock vtable `0x313258` slots `+0x0C`/`+0x10` |
| Smoothing branch | asm `0x1C240B-0x1C244C`, `0x1C25AD-0x1C2631`; `dword_316438 = 0x3EAAAAAB` = 1/3, `flt_3163C8 = 15.0`; sampler `sub_001C2750`; default off in ctor `sub_001C2070` (`app+5 = 0`) |
| Present path is render-thread only | `sub_001FC0E0` command switch (type 4 → `sub_001FC550` → `D3DDevice_Swap(0)`; type 6 → `D3DDevice_BlockUntilVerticalBlank`); `_sub_001F7EE0` is the dirty-disc screen |
| Tick forwarding forces 1 | `sub_001D98C0` pseudocode (quoted in §6) |
| Urbz shape (reference) | `D:/Urbz/src/game/hooks.cpp:45-84`, `D:/Urbz/docs/status.md:265-316`, `skill://urbz-frame-rate-and-game-speed` |
