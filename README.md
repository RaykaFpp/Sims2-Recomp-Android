# The Sims 2 - Static Recompilation

An unofficial native PC port of the original Xbox release of *The Sims 2* (USA),
produced by statically recompiling the game's x86 XBE with the
[XboxXBERecomp SDK](https://github.com/abilliontokens/XboxXBERecomp) (vendored in `sdk/`, branch `urbz-port-fixes`).

This repository contains **no game code and no game assets**. It holds tooling, configuration and
host-side glue only. You must supply your own legally acquired copy of the disc.

> **Status: boots to live gameplay.** The port plays its startup movies, reaches the title
> screen and main menu, and enters live gameplay on a lot (`docs/status.md`, update of
> 2026-10-18; screenshot `out/gameplay1.png`). A verbose 100 s bounded run through movies,
> title and menu navigation logged 0 exceptions and 0 allocation failures, and a 75 s release
> run stayed healthy with no `exception` in its log. Frame-rate and resolution measurements
> are not yet published — see below.

## Run it on Windows

There is no release download yet; build it from source (below). When it runs, you supply the
game: the USA Xbox release, as a disc image or as the files from one.

1. Build `Sims2.exe` (see **Build it from source**); it lands in `out/build/release`.
2. Put your disc image in the `assets` folder beside `Sims2.exe`, so you have
   `Sims2.exe` and `assets\<anything>.iso`. A whole-disc dump or a plain xiso both work, and so do
   extracted files (`default.xbe` with its `DATA` and `movies` folders beside it).
3. Run `Sims2.exe`. It mounts the image as the console's `D:` and boots the game.

You need Windows 10/11 x64 and a Direct3D 12 GPU.

Display features that come from the SDK and need no port work: display mode, output resolution,
render scale, aspect ratio, ultrawide, vsync, frame cap, HDR and anisotropic filtering. The port
adds an **F1** settings panel (display mode, resolution, render scale, aspect ratio, ultrawide,
vsync, frame cap, HDR, anisotropic filtering) and an **F2** frame-time graph
(`src/common/overlay.cpp`). These settings exist and are persisted by the host, but measured
frame-rate and resolution results are **not yet published** — no numbers are quoted here until
they have been measured. The host is meant to present only the drawn picture: the game draws
640x448 inside a 640x480 frame, so the host presents rows 16..463 (`picture_top = 16`,
`picture_height = 448`; see `docs/analysis-video-mode.md`). A frame-step fix in
`src/game/hooks.cpp` (guest function `sub_001C2400`) replaces the engine's 1/60-quantised frame
step so the game keeps its real speed at any frame rate instead of following the quantised
clock; see `docs/analysis-frame-step.md`.

If it says the game is missing, the `assets` folder has no image or extracted `default.xbe` in it.
The exe only runs the XBE it was recompiled from — SHA-1 `16f9e41c...`, USA retail; any other build
will not run properly.

## Controls

The game itself only ever reads an Xbox pad, so keyboard and mouse are translated into one on port 0
(`src/common/input_kbm.cpp`). A controller and the keyboard can be used at the same time; neither
takes input away from the other.

| Keyboard / mouse | Pad | In game |
| --- | --- | --- |
| `W` `A` `S` `D` | left stick | walk; menus that read the stick |
| arrow keys | D-pad | menu and dialog navigation |
| mouse, right button held | right stick | camera, deflected from where you pressed: rotate and zoom |
| `Q` / `E` | right stick left/right | rotate the camera |
| `R` / `F` | right stick up/down | zoom the camera in / out |
| mouse wheel | right stick up/down, in pulses | zoom the camera in / out |
| space | A | accept, interact |
| escape, backspace | B | cancel, back |
| `X` / `Y` | X / Y | |
| `Z` / `C` | left / right trigger | |
| `1` / `2` | black / white | |
| enter | Start | "press Start", pause menu |
| tab | Back | |
| shift / ctrl, middle mouse | left / right stick click | |

The camera is the right stick and nothing else: the game turns on its X axis and zooms on its Y.

Keys are read only while the game window has focus, and are dropped when it loses focus, so an
alt-tabbed game does not walk on by itself. While the F1 settings panel is up it owns the keyboard
and the mouse and the game sees neither.

## Status

| Stage | State |
| --- | --- |
| Disc image read and extracted (`tools/xiso_extract.py`) | done — 55 files, see `docs/iso-extract.md` |
| XBE identified and analysed (`tools/xbe_info.py`) | done — see `docs/xbe-analysis.md` |
| XDK build matched against the SDK's signature set | done — XDK 5849, supported by `sdk/xdk/5849` |
| Toolchain (LLVM clang + VS 2022 Build Tools + Ninja + CMake, Python 3.12) | done — see `docs/toolchain.md` |
| `config/game.toml` written by `xbe_symbols` | done — 132 of 240 XDK functions and 11 of 12 data symbols located, `partitions = 64` |
| Kernel import coverage (`tools/kernel_coverage.py`) | done — 124 of 124 implemented by the SDK runtime, 0 trapping, see `docs/kernel-coverage.md` |
| Recompilation and native build | done — 15,926 functions, 843,348 instructions, builds to `out/build/release/Sims2.exe` with clang/lld |
| Remaining XDK surface gaps | 68 real gaps (16 D3D, 44 DirectSound, 7 CRT math, 1 `XNetGetEthernetLinkStatus`), see `docs/analysis-xdk-gaps.md` |
| Boot and rendering | done — startup movies decode and render, title screen and main menu render, see `docs/status.md` |
| Gameplay | reached — scripted pad (Story Mode → Start New → Yes → One Player → Start) loads into a live lot; screenshot `out/gameplay1.png`, see `docs/status.md` |

Fixed during bring-up (evidence in `docs/status.md`): the `D3DDevice_Reset` crash (host
replacement plus a `[symbols]` entry), the push-buffer callback deadlock
(`D3DDevice_InsertCallback = 0x275750` plus a host replacement that runs the callback
immediately), and a physical-memory leak in `Reset` that let a read-back scribble over guest
kernel memory (surface reuse in `SetupPresentation` plus a low-physical-memory read-back guard).
Also added: `D3DDevice_SetPalette` and `D3DDevice_SetVertexShaderConstant1Fast`.

Of the 108 XDK names `xbe_symbols` did not locate, 40 are expected zeros for sections this XBE
does not have (4 Bink, 34 Winsock/XNet, 2 XOnline) and 68 are real gaps, as itemised above; the
detail is in `docs/analysis-xdk-gaps.md`.

## Analysis documents

- `docs/status.md` — authoritative bring-up record and current state.
- `docs/analysis-frame-step.md` — the 1/60-quantised frame step and its replacement.
- `docs/analysis-video-mode.md` — the 640x448 picture inside the 640x480 frame.
- `docs/analysis-startup.md` — startup sequence.
- `docs/analysis-xdk-gaps.md` — unmatched XDK surface.
- `docs/analysis-dsound.md` — DirectSound analysis.
- `docs/analysis-dsound-xtlid.md` — DirectSound `.XTLID` exports.
- `docs/analysis-4k-hazards.md` — resolution hazards and the ordered scaling plan.
- `docs/toolchain.md` — toolchain and build presets.
- `docs/iso-extract.md` — disc-image extraction.
- `docs/xbe-analysis.md` — XBE identification and analysis.
- `docs/kernel-coverage.md` — kernel import coverage.

## The game

| Field | Value |
| --- | --- |
| Title | The Sims 2 (USA), original Xbox |
| Title ID | `0x4541038B` (EA, serial 907) |
| Region | North America (`north_america`) |
| Allowed media | `dvd_x2`, `media_board` |
| Build | retail, base `0x00010000`, entry `0x0021BF66`, image size 4,134,528 bytes |
| XBE | `default.xbe`, 3,399,680 bytes, SHA-1 `16f9e41ce26c315a758f25d43ba1fa9d3809d1ad` |
| Linked | 2005-09-12T12:10:50Z (certificate 2005-09-22T17:21:41Z) |
| Debug name | `c:\buildagent\cm3-build16-win32\cmbuild\output\disks\xbox\u2_xbox_release_dvd.exe` |
| XDK | 5849 — XAPILIB 1.0.5849 qfe1, XBOXKRNL 1.0.5849 qfe1, LIBCMT 1.0.5849 qfe1, D3D8 1.0.5849 qfe15, XGRAPHC 1.0.5849 qfe9, DSOUND 1.0.5849 qfe11, XMV 1.0.5849 qfe1 |
| Kernel imports | 124, thunk table at `0x002DED60` |
| Sections | `.text`, `D3D`, `XGRPH`, `DSOUND`, `XMV`, `XPP`, `.rdata`, `.data`, `DOLBY`, `$$XTINFO`, `$$XTIMAGE`, `$$XSIMAGE`, `.XTLID` — no BINK, XNET or XONLINE sections |

The build is pinned to this exact XBE. A different region or revision needs its own analysis pass.

Recompilation, for the record: 15,926 functions, 843,348 instructions, 2,694,811 of 2,940,256 code
bytes, 524 jump tables, 10 callback entries.

## Layout

```
assets/        your disc image and/or the files extracted from it (never committed)
config/        game.toml - the recompiler config written by xbe_symbols, then hand-corrected
docs/          analysis and bring-up notes
sdk/           XboxXBERecomp SDK submodule (recompiler, runtime, XDK HLE, D3D12 renderer)
src/common/    host-side code: game_info.cpp defines app::kGame, input_kbm.cpp the keyboard pad,
               overlay.cpp the F1 settings panel and F2 frame-time graph
src/game/      hooks and overrides for recompiled functions (src/game is excluded from the host glob)
tools/         xiso_extract.py (XDVDFS reader), xbe_info.py (XBE reader), kernel_coverage.py,
               env.bat (toolchain PATH)
generated/     recompiler output (never committed)
out/           build output, out/build/<preset> (never committed)
```

## Build it from source

1. Put your disc image in the repository root, or `default.xbe` plus its `DATA` and `movies`
   folders in `assets/`. Do not commit the image; `*.iso` and `assets/` are git-ignored.
2. Get the SDK submodule: `git submodule update --init sdk`
3. Extract the image:
   ```
   python tools/xiso_extract.py info    "<your>.iso"
   python tools/xiso_extract.py extract "<your>.iso" assets
   ```
4. Check that the XBE is the one this configuration targets:
   ```
   python tools/xbe_info.py assets/default.xbe --json build/xbe_info.json --markdown docs/xbe-analysis.md
   ```
   The SHA-1 must match the table above.
5. Install the toolchain and build:
   ```
   tools\env.bat
   build.bat
   ```
   `build.bat` configures the `release` preset, builds `xbe_symbols`, writes `config/game.toml`
   from the XBE and the SDK's XDK 5849 signatures, then recompiles the XBE and builds `Sims2.exe`
   into `out/build/release`. The first build is long: it compiles one C++ function per guest
   function. Presets, and a separate verbose RelWithDebInfo build directory for debugging, are
   described in `docs/toolchain.md`.

## Working on the port

- Never edit anything in `generated/`; it is rewritten on every recompile.
- Behaviour changes go in `src/game/` as `X86_FUNC(sub_XXXXXXXX)` overrides (call
  `__imp__sub_XXXXXXXX` for the original) or as `[[hook]]` entries in `config/game.toml`.
- Analysis mistakes are corrected in `config/game.toml` with `[[function]]`, `[[switch]]` and
  `[xbe] data_ranges`.
- `python sdk/tools/symbols/xbe2pe.py config/game.toml game.exe` writes a flat PE with the
  recompiled functions as exports, for symbol work in Ghidra.

## Legal

Bring your own disc. Do not commit or distribute the XBE, the disc image, extracted assets, or
any key material. `assets/`, `generated/`, `*.iso` and `out/` are ignored by git for that reason.
The SDK is MIT licensed; see `sdk/LICENSE` and `sdk/THIRD_PARTY.txt`.
