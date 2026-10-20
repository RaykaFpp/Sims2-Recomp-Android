# Bring-up status — The Sims 2 (USA), XDK 5849

Recorded 2026-10-18 on this machine. Builds: `out/build/release` (NDEBUG, quiet log)
and `out/build/verbose` (RelWithDebInfo, full log). Every run below was launched
with a bounded lifetime (`timeout`) and killed; no window was left up.

## Where the port stands

| Stage | Result |
| --- | --- |
| Recompilation / link | `out/build/release/Sims2.exe`, 15,926 guest functions (unchanged) |
| Boot | Boots, creates the D3D device, loads the data archives, opens the first movie |
| D3D initialisation | **Fixed** — was a crash at ~0.41 s, now passes `D3DDevice_Reset` and presents |
| Presents | 4 swaps in the first ~0.4 s (see evidence), then no further presents |
| Title screen | **Not reached** — the game stalls in XMV movie playback (below), it is not waiting for a button press |

## Defect 1: crash in guest D3D display-driver code during `Reset`

Before (release, `out/build/release/logs/sims2.log`, reproduced 2026-10-18 17:58):

```
[    0.406] error: exception C0000005 at 00007FF7DDEA47AA on guest thread 24
[    0.406] error:   read 100003214, guest 00003214
[    0.406] error:   eax D017FDE4 ecx 00283798 edx 00000000 ebx 00000A00 ...
[    0.406] error:   [esp+008] 0027AD8D
[    0.406] error:   [esp+0F8] 002715A3
[    0.406] error:   [esp+110] 001FCB5F
```

Diagnosis (full analysis in `docs/analysis-xdk-gaps.md` §1): game code calls the
guest body at `0x271570`, which is `D3DDevice_Reset` (168 bytes, `stdcall`, one
presentation-parameters pointer, `ret 4`; verified in `build/sims2_flat.exe`:
it calls `D3D_KickOffAndWaitForIdle`, tears down surfaces, sets the display mode
through the NV2A miniport at `CDevice+0x1C28`, then calls the located
`D3DDevice_SetRenderTarget` and `D3DDevice_Clear`). The host had no replacement
for it, so the guest driver spun on `*(miniport)+0x3214` with the miniport
pointer never filled by host `Direct3D_CreateDevice` — `esi = 0`, fault at
guest `0x3214`. `D3DDevice_Reset` is not in the SDK's XDK symbol set at all
(no entry in `sdk/xdk/5849/signatures.toml`), so this needed new host code, not
just a config line.

Fix:

- `sdk/runtime/xdk/d3d8.cpp`: new `HLE_STDCALL(D3DDevice_Reset, 1)`. It does what
  a host renderer needs, not what the console did: flush (the guest's
  kick-and-wait), then apply the presentation parameters (width `+0x00`, height
  `+0x04`, format `+0x08`, count `+0x0C`, multisample `+0x10`, flags `+0x28`,
  refresh `+0x2C`, interval `+0x30`) through `SetupPresentation`, a helper
  extracted from `Direct3D_CreateDevice` so both share one code path (frame
  surfaces, `g_frame_w/h`, back/front/render-target/depth device fields at
  `0x1A14/0x1A18/0x1A04/0x1A08`, auto-depth `0x1A20`, display flags, viewport,
  `g_state.rt/zs`), then clears (`Clear(0,0,3,0,1.0f,0)`, the guest tail) and
  returns S_OK (`0x88760869` only for a null parameter pointer).
- `Reset` does **not** wipe bound state: an earlier revision reset
  `g_state_data` and re-applied default states like `CreateDevice`; the guest
  body does neither (no `D3D_InitDefaultState` call), and the first post-Reset
  draw then arrived with no shader/stream bound (`draw skipped … vs 00000000`).
  `SetupPresentation(..., fresh)` now resets state only for `CreateDevice`.
- `config/game.toml`: `D3DDevice_Reset = 0x00271570` in `[symbols]`
  (alphabetical position, existing alignment kept).

After (verbose, 2026-10-18 18:17 run, `out/final_verbose.txt` in the build tree):

```
[    0.079] d3d: CreateDevice 640x480 format 1E buffers 1 depth 1 format 2E interval 2 flags 130
[    0.274] d3d: Swap 1
[    0.291] d3d: Swap 2
[    0.348] d3d: Swap 3
[    0.365] d3d: Reset 640x480 format 1E buffers 1 multisample 11 depth 1 format 2E flags 130 refresh 0 interval 1
[    0.369] d3d: Swap 4
```

The game requested exactly the mode `CreateDevice` already had (640×480, format
`0x1E`, one buffer, depth on), so the host surfaces are recreated identically.
Release build after the fix: runs the full bounded 30–32 s with **no**
`exception` line in `logs/sims2.log` (before: dead at 0.406 s). The two swaps
before the Reset are the post-`CreateDevice` `Clear+Swap` pair in guest
`sub_001FB1CD`; swaps 3–4 bracket the mode change on the render thread.

## Defect 2 (evaluated symbols from `docs/analysis-xdk-gaps.md` §4)

Both proposed entries were checked against the flat image and added:

- `D3DDevice_SetPalette = 0x00272020` — **confirmed**. Guest body is
  `__stdcall(stage, header)` (`ret 8`), reads/releases/stores the header at
  `CDevice+0xF98+stage*4` (`CDevice_Palettes`, `sdk/xdk/5849/layout.toml:14`,
  whose comment names `SetPalette` as the storer), refcounts it (`+0x80000`),
  and pushes `(stage<<6)+0x41B20` with the header data — exactly the host
  `HLE_STDCALL(D3DDevice_SetPalette, 2)` in `d3d8.cpp` and the Urbz
  counterpart's documented shape. No signature length exists for this name
  (`{}` in `signatures.toml`), so nothing contradicts it.
- `D3DDevice_SetVertexShaderConstant1Fast = 0x00272FD0` — **confirmed, with the
  length discrepancy recorded**. The signature records `len = 0x49` (73) and
  `ret = 0`; the candidate at `0x272FD0` is 49 bytes (`0x272FD0–0x273001`) with
  `ret 4`. Against that, the shape is exact and unique: `__fastcall`
  (register in `ecx`, source in `edx`, dword count on the stack),
  `rep movsd` copying count dwords to the fast-constant scratch at
  `0x280AB0 + 16*reg` (skipped when the device flag `+0x08 & 0x10` is set),
  then a tail call to the located `D3DDevice_SetVertexShaderConstantNotInlineFast`
  (`0x272EF0`) with `(reg, source, count)` still in place — precisely what the
  host implementation's comment describes ("the guest original also copies the
  data into the fast-constant scratch … before tail-calling
  `SetVertexShaderConstantNotInlineFast`", `d3d8.cpp`). Neighbour order also
  matches the sibling title exactly (`…Constant1`, `…Constant4`,
  `…NotInlineFast`, this function, `SetStreamSource 0x273010`), and an IDA scan
  for the scratch address found no other function with this shape. The
  recorded length/`ret` come from the reference build the signatures were
  captured on; this title's shorter body is why the matcher missed it. If this
  judgement is ever doubted, the safe revert is setting the entry back to `0`
  (the host body then simply never binds).

## Current blocker: XMV movie playback stalls after the first frames

After swap 4 the game stops presenting. Evidence from 20–60 s verbose runs
(`XBE_MUTE=1`, with and without `XBE_PERF=1`, with and without the sibling
scripted pad `SIMS2_AUTOSTART=1`):

- No `exception` line, process stays alive; pad polls keep being answered
  (autostart `synthetic Start/A …` logs fire on schedule) and change nothing —
  so this is **not** an input wait. A sibling agent's scripted-pad harness
  (`SIMS2_AUTOSTART` / `SIMS2_STEP`) exists for when a press is actually needed.
- Just before the Reset the game opens `D:\movies\ea_games.xmv`. The movie
  frame path is guest code: `sub_001FF630` calls `sub_0021A5F0` (XMV
  get-frame, `sub_002AFF8D`) and then `D3DDevice_DrawVerticesUP(8, 4, …, 24)`
  for the fullscreen quad. Exactly one such quad draw reached the backend
  (first runs: `d3d12: draw skipped, nothing to assemble: rt … prim 8 count 4
  vs 00000000`; after the state-preserving Reset it assembles, still only
  once). No further draws, swaps, file opens or D3D calls follow.
- The SDK has no XMV host support (its video path is Bink, `sdk/runtime/xdk/bink.cpp`;
  all `Bink*` are correctly `0` here). XMV playback is recompiled guest code in
  the `XMV` section (`0x2AF920–0x2D73C0`) driving a WMV decoder through
  callbacks (`sub_0021A670`/`sub_0021A630`); on failure `sub_001FF630` calls
  the fatal disc-error screen `sub_001F7EE0`, whose `Swap → Reset → Swap`
  prefix is indistinguishable in the log from a normal present + mode change.
  Which of "decoder never yields a second frame" vs "fatal screen entered and
  blocked in its text path" applies is **unconfirmed** — the two share the
  observed prefix, and per-thread guest stacks could not be resolved further
  with the tools at hand (no native debugger on this machine; the one busy
  thread samples as the guest idle loop `sub_001DABD0`, which is expected).

Next steps for whoever picks this up: instrument the XMV get-frame return
(`sub_002AFF8D` / its decoder callback at `movie+28`) and the fatal-screen
entry (`sub_001F7EE0`) — e.g. host hooks in the game-hooks file, not the SDK —
and either bring up XMV decode or use the game's own `skip_startup_movies`
setting (registered in `sub_0002A300`) to reach the title screen, whose menu
draws (tens per frame) would then be the next check. Minor, probably
unrelated: the push walker still drops methods `1D8C/1D90/0110/0100`
(logged once each at Reset time).

## Verification summary

- Before: release crash `C0000005 … guest 00003214` at 0.406 s (log above).
- After: release 32 s bounded run, log clean; verbose 30 s bounded run,
  `CreateDevice` → `Swap 1,2` → `Swap 3` → `Reset 640x480 …` → `Swap 4`,
  no exception. The port is past D3D initialisation and presenting its first
  frames; it is the movie subsystem, not D3D setup, that blocks the title
  screen.

## Update 2026-10-18 (later): title screen, menus and gameplay reached

The "current blocker" above is resolved. Two further defects were found and
fixed; the game now plays the startup movies, reaches the title screen and
main menu, and enters live gameplay. Temporary probe hooks used for the
diagnosis (`src/game/stall_probe.cpp`, allocator logging) were removed again;
what remains is `src/game/d3d_callback.cpp`, one `[symbols]` line, the
`SetupPresentation` reuse in `d3d8.cpp`, and a read-back guard in `d3d12.cpp`.

### Defect 3: push-buffer callback deadlock after the first movie frame

The two candidates were instrumented (entry hooks on `sub_001F7EE0` and
`sub_002AFF8D`, plus an `lldb` attach): the fatal disc-error screen was
**refuted** — its entry never fired and guest byte `0x3B81E0` read `0x00` at
the stall. The XMV get-frame fired exactly once (`exit eax=0`), one movie
quad was drawn, the render pump (`sub_001FC0E0`) dispatched 12 tasks and
stopped, and every guest thread then idle-waited. Root cause: the pump's
kick task (op 3) calls guest `0x00275750`, which is `D3DDevice_InsertCallback`
(confirmed in the flat image: writes token `0x00081D8C` plus the
`0x00040100`/`0x00040110` words, `retn 0Ch`, neighbour order
`IsFencePending 0x275730` → this → `CDevice_KickOff 0x2757C0`, the sibling
port's spacing exactly). The name is absent from
`sdk/xdk/5849/signatures.toml`, so no `[symbols]` entry existed and the
guest body ran: it tokenised the callback into the push buffer, the SDK
walker dropped the token (`Walk drops method 1D8C / 0110 / 0100` in the old
logs), the callback `sub_001FA8C0` never ran, and the engine thread waited
forever on the semaphore only that callback releases.

Fix: `D3DDevice_InsertCallback = 0x275750` in `config/game.toml` and
`src/game/d3d_callback.cpp`: `HLE_STDCALL(D3DDevice_InsertCallback, 3)` runs
`pCallback(Context)` immediately via `kernel::CallGuest` (the host renderer
has no GPU timeline for the guest to lag behind), mirroring the Urbz fix.

Before (verbose, `sims2_023.log`): `Swap 1,2 → Swap 3 → Reset → Swap 4`,
one `movie-draw`, then no dispatches for 25 s. After (verbose,
`sims2_024.log` / `sims2_025.log`): get-frame called continuously
(`#720`, `#840`, …), movies play at 60 fps with 1 draw and ~0.5 MB texture
upload per frame, `open D:\movies\s2c_intro.xmv` follows `ea_games.xmv`,
then 110–130 draws/frame in the 3-D main menu. The deadlock is cleared.

### Defect 4: crash entering a new game (owner-reproduced)

Driving the menus (scripted pad: Story Mode → Start New → Yes → One Player →
Start) reproducibly crashed at load, also seen by the owner with the same
host address `0x00007FF7BC3792F9`:

```
[   72.404] [error] exception C0000005 at 00007FF7BC3792F9 on guest thread 18
[   72.404] [error]   read 1FF000024, guest FF000024
```

Faulting guest function (minidump + `lldb` core): `sub_00265696`, XAPI's
cancel-pending-I/O walk over the IDE channel device queue at
`IdexChannelObject+0x28`, reached from a wavebank destructor
(`sub_002ACF21 → sub_002ACCE1 → sub_00265696`). The walk read node
`0xFF000000` because the whole channel object had been overwritten with
`FF000000` dwords. A watchdog on the kernel data showed the overwrite at
71.536 s, microseconds after `d3d12: CPU pixels drawn over target 80000000`:
a 640×480 surface (header `B010A790`, `data=00000000`) was locked, and the
backend's read-back wrote the (black) target over physical 0 — the first
physical megabyte, where the host keeps its kernel variables.

Why the surface had no memory: contiguous (physical) space was exhausted.
The guest claims ~44 MB of pools at boot; every host `Reset` then allocated
a fresh front/back/depth triple (~3.6 MB) in `SetupPresentation`
("Old surfaces are leaked, not freed" — allocator log showed four triples
allocated, none freed) until `contiguous allocation of 12C000 bytes failed
(…, 62812 KB in use)` and later surface allocations returned physical 0.

Fix, two parts:

- `sdk/runtime/xdk/d3d8.cpp` `SetupPresentation`: a `Reset`/`CreateDevice`
  asking for the presentation already in place (same size, format, buffer
  count and depth setup) now reuses the existing frame surfaces instead of
  allocating a new triple. Changed parameters still allocate (and leak, as
  before — the title may hold the old headers, and a size change is rare).
- `sdk/runtime/gpu/d3d12.cpp`: `Sync`/`DeliverLate` refuse a read-back whose
  guest address lies in low physical memory (`< 0x80100000`). No surface can
  live there (the allocator starts above the kernel megabyte), so this can
  never write pixels over kernel variables again, whatever made the surface.

After (verbose, `sims2_034.log`): zero `contiguous allocation` failures, no
physical-0 surface, no exception, and the same pad sequence runs past the
old crash point into live gameplay — `[ 120.454] perf: per frame 874 draws
… 41k verts`, rising to ~1600 draws as the lot fills. The owner's exact
crash no longer occurs on this path.

### The five "original code" DSOUND exports — evaluated, not named

The boot audit lists `B00A3 at 002A5A36, B00A5 at 002A5A80, B00A4 at
002A5A85, B0080 at 002A69AC, B00A9 at 002A69ED`. These ids have **no name**
in `sdk/xdk/5849/signatures.toml` (its named DSOUND xtlids end at
`0xB00A6 XAudioDownloadEffectsImage`), and none of the five addresses is in
the Defect-4 crash stack (its DSOUND frames are the located
`IDirectSoundBuffer_Release` chain into the wavebank destructor). On
inspection `B0080` is a 4-argument call-forwarder to the already-hosted
`CDirectSoundStream_FlushEx` — flagged only because the audit recognises
`jmp` thunks, not `call` forwarders — and the others are small library
internals beside the `Release` entries (the sibling port lists the same
five as unidentified). Inventing `[symbols]` names for them would replace
unknown functions with guessed ones, so they are left running their guest
bodies; the earlier `docs/analysis-dsound.md` conclusion stands for the
named API, with this caveat recorded.

### Where it stands now

- Startup movies decode and render (F3 capture `out/cap_movie_render.png`
  shows an `s2c_intro` frame), the "Please press START to begin" title
  screen renders, and the main menu (3-D neighbourhood, ~115 draws/frame)
  navigates with the scripted pad: RIGHT moved Story Mode → Sims Mode
  (Freeplay) (`out/final_menu*.png` from F3 captures `out/cap_final/`),
  A/B move in and out of submenus, and Story → Start New → Yes → One Player
  loads into a live lot: Sim on the lawn, HUD clock running, needs task
  prompt up (`out/gameplay1.png`, foreground screenshot of that run).
- Verification: verbose 100 s bounded run (`sims2_035.log`) through movies
  → title → menu navigation: 0 exceptions, 0 allocation failures, presents
  throughout. Release build: 75 s bounded run, process healthy throughout,
  no `exception` in `out/build/release/logs/sims2.log`. Both builds compile
  clean after the probe removal. Runs were launched individually, bounded,
  and killed by PID; no window was left up.

## Update 2026-10-19: native Linux target (Vulkan)

Recorded on this Linux machine: CachyOS, kernel 7.2.11, clang 23.1.3, AMD
BC-250 (RADV GFX1013, Vulkan 1.4.363), 3440x1440 display. Every run below was
bounded with `timeout` and killed; no window was left up.

The SDK submodule was fast-forwarded from `30124a8` to `adc40ab` on
`sims2-port-fixes` (15 commits ahead, 0 behind, so no sims2 work is lost):
that range is what brings `runtime/gpu/vulkan.cpp` and `spirv.cpp`, the SDL3
host and audio, the POSIX kernel/file/iocp layer, and the cross-build support.

Port-side work was three files, all of it the Windows-only assumptions in the
host glue:

- `src/common/input_kbm.cpp/.h` — the Win32 `windows.h` collection path became
  one of two. Bindings are byte-for-byte the same; they now fill a logical
  `Key` enum that either `kbm::Message` (Win32) or `SDL_GetKeyboardState` plus
  the `NoteMouse`/`NoteWheel` cache (SDL) populates, and one `Apply()` merges
  the snapshot into the pad. `SDL_GetMouseState` is main-thread-only, so the
  window thread publishes the sample through atomics instead of guest polling
  threads calling it.
- `src/common/game_info.cpp` — `.message` is the Win32 hook only; the SDL host
  calls no message hook. Building for Linux failed on exactly this
  (`no member named 'NoteMouse' in namespace 'kbm'`, `'windows.h' file not
  found`).
- `src/common/overlay.cpp` — Hor+ widening is a backend property: D3D12 widens
  the frame target by `app::AspectWiden()`, Vulkan to `app::HostAspect()` at
  the same height. `BackendWiden()` makes the F1 panel report the target the
  active backend builds instead of the D3D12 one on both. The wheel reaches
  the pad through `kbm::NoteWheel` where there are no window messages.

Results:

| Run | Result |
| --- | --- |
| `out/build/linux-release/Sims2`, 75 s | Vulkan adapter selected, 0 `error`/`exception` lines, clean `Exit (0)` |
| Same binary, 70 s, scripted pad (`SIMS2_AUTOSTART=1`) | title screen with the Story Mode menu up (`out/shots/drive_a.png`), then a 3-D neighbourhood scene (`out/shots/drive_c.png`) |
| Packaged build (`release.sh`), extracted to `/tmp` and run there, 60 s | renders the startup movie; every bundled library resolves inside the package (`DT_RPATH $ORIGIN/lib`), 0 errors |
| The published `Sims2-linux-x64.tar.zst`, re-downloaded from the v0.2.0 release and run from a scratch extraction, 80 s | startup movies, then the save-game selection screen over the 3-D neighbourhood — past title and main menu; 0 errors |

In that last run the save-select dialog's prompt text draws semi-transparent over the
neighbourhood behind it. Whether the D3D12 build does the same has not been checked, so it is
not established as a Vulkan-only difference; recorded here rather than claimed either way.

The host reports `Desktop: 1280x720` on this KDE/Wayland session while the
window is in fact the full 3440x1440; the sibling Bustin' Out port logs the
same line on this machine, so it is an SDK display-query quirk, not something
this port introduced. Not chased here.

### Windows exe cross-built from Linux

`cmake/windows-cross.cmake` plus the `host-tools` and `windows-cross` presets
build `Sims2.exe` with clang-cl and lld-link against an xwin sysroot; the
recompiler runs natively and both targets share one `generated/` tree. The
result imports `MSVCP140.dll`, `VCRUNTIME140.dll` and `VCRUNTIME140_1.dll`
(VC++ redistributable) and otherwise only what ships with Windows 10+.

It has **never been run on real Windows.** Under Wine 11.20 with vkd3d it
loads the XBE, creates the D3D12 device and swap chain and renders the startup
movie (`out/shots/wine_a.png`), with 0 shader-compilation failures and 0
errors in a 60 s bounded run. That is the whole of the evidence for that
binary.
