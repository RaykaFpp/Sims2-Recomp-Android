# Video mode, picture rectangle and resolution hazards — The Sims 2 (Xbox, USA)

Static analysis of `build/sims2_flat.exe` (guest addresses = flat-PE addresses, image base
`0x00010000`) with IDA. No build was run. Host-side references are to the vendored SDK at
`sdk/` and to `D:/Urbz/src/common/game_info.cpp` (the sibling port's `picture_top` comment).
Anything not directly observed is marked UNCONFIRMED.

## 1. Device creation and presentation parameters

### Call chain (all guest addresses)

1. Display object method `sub_001C33D0` (display vtable `0x313898` slot 3, i.e. call at
   `[vtable+0x0C]`) builds a 5-dword device config at `display+700` (`this+0x2BC`) and calls
   the Renderer through `dword_3B81FC` vtable slot 3 at `0x1C35C7` (`call dword ptr [edx+0Ch]`,
   config pointer pushed at `0x1C35C6`).
2. Renderer slot 3 is `sub_001FB080` (Renderer vtable starts at `0x3158AC`; slot 3 entry at
   `0x3158B8`). It fills a `D3DPRESENT_PARAMETERS` block at `Renderer+36408` (`+0x8E38`) and
   calls `Direct3D_CreateDevice` (located at `0x270AB0` in `config/game.toml`) at `0x1FB1B8`,
   with `BehaviorFlags = 0x40` (push at `0x1FB181`) and the device out-pointer at
   `Renderer+124`.
3. The guest XDK worker `sub_002781B0` (called from `Direct3D_CreateDevice` at `0x270B09`)
   confirms the field layout: it forwards `pp[0], pp[1], pp[11], pp[10], pp[2], pp[12]` to the
   mode matcher `sub_002775A4`, and `sub_00277C10` reads `pp[3]` as back-buffer count,
   `pp[4]` as multi-sample type (defaulting to 17 when 0, split into X/Y sample counts as
   floats), `pp[5]` as swap effect, `pp[8]` as auto-depth enable and `pp[9]` as depth format.

### The config the game builds (`sub_001C33D0`)

The mode is chosen from the video flags returned by `sub_00260BC2` (see §1.3). The config
struct at `display+700` is `{width, height, refresh, flags, format}`; `sub_001FB080` maps it
into the presentation block as `pp.width=cfg[0]`, `pp.height=cfg[1]`, `pp.format=cfg[4]`,
`pp.flags=cfg[3]`, `pp.refresh=cfg[2]`.

| Mode (`display+744`) | BackBufferWidth × Height | cfg flags (`display+712`) | Picture size (`display+12` × `display+16`) | Centre offsets (`display+728`, `+732`) |
|---|---|---|---|---|
| 0 (default) | 640×480 (`0x1C34FA`, `0x1C358A`) | `0x120` (`0x1C3500`) | 640×448 (`0x1C350A`, `0x1C3594`) | 0.0, 16.0 (`0x1C34F7`→`0x1C357A`, `0x1C3582`→`0x1C359D`) |
| 1 (flag bit 3) | 720×480 (`0x1C3563`, `0x1C358A`) | `0x40` (`0x1C3569`) | 680×448 (`0x1C3573`, `0x1C3594`) | 20.0, 16.0 (`0x1C3555`, `0x1C3582`) |
| 2 (flag bits 0+1) | 1280×720 (`0x1C352B`, `0x1C3531`) | `0x40` (`0x1C353B`) | 1152×648 (`0x1C3545`, `0x1C354C`) | 64.0, 36.0 (`0x1C3513`, `0x1C3523`) |

In every mode `cfg[2]` (refresh) is stored as 0 at `0x1C35B4` and `cfg[4]` (format) as 30 at
`0x1C35BA`. If video-flag bit 0 is set, `0x10` is OR-ed into the cfg flags at `0x1C34DF` and
again at `0x1C35A7`. The mode variable itself is written at `0x1C34B1` (0), `0x1C34BA` (1)
and `0x1C34D1` (2).

### Resulting `D3DPRESENT_PARAMETERS` (`sub_001FB080`, block base `Renderer+0x8E38`)

| Field | Value | Store address |
|---|---|---|
| BackBufferWidth | cfg[0]: 640 / 720 / 1280 by mode | `0x1FB145` |
| BackBufferHeight | cfg[1]: 480 / 480 / 720 by mode | `0x1FB14A` |
| BackBufferFormat | 30 = `LIN_X8R8G8B8` (SDK table `sdk/runtime/gpu/texture.cpp:48`) | `0x1FB153` |
| BackBufferCount | 1 | `0x1FB15E` |
| MultiSampleType | 17 (`0x11`, i.e. 1×1 — no supersampling; split as X/Y counts in `sub_00277C10`) | `0x1FB18E` |
| SwapEffect | 1 | `0x1FB198` |
| EnableAutoDepthStencil | 1 (true) | `0x1FB19E` |
| AutoDepthStencilFormat | 46 = `LIN_D24S8` (`sdk/runtime/gpu/texture.cpp:60`) | `0x1FB1A4` |
| Flags | cfg[3]: `0x120` (mode 0) or `0x40` (modes 1–2), plus `0x10` when video-flag bit 0 is set. Raw values confirmed; the Xbox names of these bits are UNCONFIRMED. | `0x1FB170` |
| FullScreen_RefreshRateInHz | cfg[2] = 0 (driver default) | `0x1FB167` |
| FullScreen_PresentationInterval | 2 | `0x1FB1AE` |

Corroboration: the disc-error screen at `sub_001F7EE0` builds its own presentation block on
the stack with width 640, height 480, format 30 and multi-sample 17 (stores at `0x1F7FE4`–
`0x1F7FEC` region, passed to the XDK reset helper `sub_00271570`), i.e. it assumes the same
640×480 / format-30 device.

### 1.3 Which video flags are consulted

The game does **not** call `XGetVideoFlags`, `XGetVideoStandard` or `AvGetSavedDataAddress`
by name anywhere in `.text`. All uses of the AV kernel thunks (`AvGetSavedDataAddress`
`0x2DEF1C`, `AvSendTVEncoderOption` `0x2DEF20`, `AvSetDisplayMode` `0x2DEF24`,
`AvSetSavedDataAddress` `0x2DEF28`) are inside the guest XDK D3D library (`0x2743D0`,
`0x277AE0`, `0x27B026`, `0x27750C`), not in game code.

Mode selection instead reads the dashboard settings through the XAPILIB helper
`sub_00260BC2` (`0x260BC2`):

- `ExQueryNonVolatileSetting` (kernel ordinal 24, thunk `0x2DEE08`, wrapper `sub_00220B46` at
  `0x220B46`) index **8** (`XC_VIDEO_FLAGS`); the result's high word is masked with `0x5F` at
  `0x260BE4`.
- `HalBootSMCVideoMode` (kernel ordinal 356, thunk `0x2DEEF4`, reader `sub_00261C41` at
  `0x261C41`) and setting index **259** (`0x103`, reader `sub_00261C18` at `0x261C18`): if the
  SMC mode is not 1, or the factory AV-region byte is 3, bits 1–3 are cleared at `0x260C02`
  (`and [var], 0xFFFFFFF1`), disabling modes 1 and 2.

Host consequence (confirmed from host code, not from the guest): the SDK kernel returns
`VideoFlags() = (widescreen ? 0x00010000 : 0) | 0x00080000` for setting 8
(`sdk/runtime/kernel/xboxkrnl.cpp:1226,1233`) but sets `HalBootSMCVideoMode` to 0
(`sdk/runtime/kernel/xboxkrnl.cpp:98`). The guest therefore masks off bits 1–3 at `0x260C02`
and **always selects mode 0 (640×480) on this host**, regardless of the widescreen setting
(which survives as bit 0 and only adds present-flag `0x10` and the 16:9 aspect switch in
`sub_001C32C0`, returning 1.7777 from `0x3162D4` when `display+748` is set). Modes 1–2 are
reachable only if the host kernel's SMC video mode is changed to 1.

`D3DDevice_SetRenderTarget` is not part of device creation; its game call sites are listed
in §3.

## 2. The true drawn picture rectangle

The game does **not** fill the whole 640×480 back buffer in mode 0. It draws a 640×448
picture centred vertically, exactly like the sibling title. Evidence:

1. **Picture size fields.** In mode 0 the display object stores picture width 640 at
   `display+12` (`0x1C350A`) and picture height 448 at `display+16` (`0x1C3594`), alongside
   the 640×480 back buffer (§1). In modes 1–2 the same fields hold 680×448 and 1152×648.
2. **Centre offsets are exactly `(backbuffer − picture) / 2`.** `display+728` / `display+732`
   hold (0.0, 16.0) in mode 0, (20.0, 16.0) in mode 1 and (64.0, 36.0) in mode 2
   (`0x1C357A`, `0x1C359D`, `0x1C3513`, `0x1C3523`). (640−640)/2 = 0, (480−448)/2 = 16,
   (720−680)/2 = 20, (1280−1152)/2 = 64, (720−648)/2 = 36. These floats are only meaningful
   as the picture's top-left offset inside the back buffer.
3. **Screen-space offset constants.** The Renderer computes
   `offsetX = (float)display[+20] + display_float[+728] + 0.53125` and the same for Y with
   `+24`/`+732`, at `0x1FD735`–`0x1FD75F` in `sub_001FD6D0` (and identically in
   `sub_001FB080`, `sub_001FD300`, `sub_001FF020`). `display[+20]`/`display[+24]` are zeroed
   by the base constructor `sub_001D95F0`; `0.53125` is the constant at `0x316418`. The
   offsets are stored at `Renderer+0x880`/`+0x884` and fed to the vertex-shader screen
   constants (constant 121 built in `sub_001FB3F0`, constant 165 in the post passes).
4. **Scissor rects are shifted by the same offset.** `sub_001FD6D0` adds the X/Y offsets to
   every scissor corner (float adds at `0x1FD767`–`0x1FD7B2`), clamps against the back-buffer
   size obtained through display vtable slots 40/41 (`0x1C3320` returns `display+700`,
   `0x1C3330` returns `display+704` — the config width/height), and calls
   `D3DDevice_SetScissors` at `0x1FD931`. When rendering into an off-screen target (the
   other branch, from `0x1FD820`) the offset is forced to 0, confirming the offset exists
   only to centre the picture in the back buffer.
5. **The 2D screen matrix is built from the picture size.** In `sub_001FB3F0` the floats
   `(float)*(int*)(display+12)` and `(float)*(int*)(display+16)` are stored into the screen
   matrix at `Renderer+1776` (`0x1FBBC7`) and `Renderer+1796` (`0x1FBBCF`), which is then
   uploaded as vertex-shader constants. The 2D coordinate space is therefore 640×448, not
   640×480.
6. **Viewport.** No game (`.text`) code calls `D3DDevice_SetViewport` (`0x271C40`); its only
   callers are XDK-internal (`sub_002748A0` at `0x2748A0`, `sub_00276CB0` at `0x276CB0`),
   which derive the viewport from the bound render target's size. The host likewise
   initialises the viewport to the full back buffer (`sdk/runtime/xdk/d3d8.cpp:386`). The
   picture is positioned by the offset constants and scissors above, not by a viewport.
7. **UI space is 640×448.** The reciprocal constants `1/640` at `0x316478` and `1/448` at
   `0x316474` are multiplied into UI coordinates at `0x168B7`/`0x168CC` (`sub_00016770`) and
   `0x12EC11`/`0x12EC23` (`sub_0012EB30`). Tiny getters `sub_00014DB0` (`0x14DB0`) and
   `sub_00014DC0` (`0x14DC0`) return the float constants 640.0 (`0x3163F8`) and 448.0
   (`0x3163F4`) respectively.

### Recommendation for `src/common/game_info.cpp`

The current file (`D:/sims2/src/common/game_info.cpp:23-25`) leaves the picture uncropped.
For the mode this host actually runs (mode 0, and also mode 1, which shares the same
vertical geometry), it should be:

```cpp
.picture_top = 16,
.picture_height = 448,
```

This presents guest rows 16..463 of the 640×480 frame, matching the sibling port's values
in `D:/Urbz/src/common/game_info.cpp:178-179` (there justified by a measured 640×448 screen
matrix and a 640×448 intro movie; here justified by items 1–7 above).

Caveats:

- Mode 2 (1280×720, unreachable on this host per §1.3) has picture top 36 and height 648. If
  the host kernel is ever changed to report SMC mode 1, these `game_info` values would crop
  mode-2 frames incorrectly (they would still be right for modes 0–1).
- Mode 1 (720×480) also has 20-pixel black bars on the left/right (picture 680 wide centred
  in 720). `picture_top`/`picture_height` only crop rows (host presentation in
  `sdk/runtime/gpu/d3d12.cpp:3395-3402` builds a UV rect from rows only), so mode-1 side bars
  would remain visible. This does not affect the host's current mode-0 path.
- The native resolution of the `.xmv` movies is UNCONFIRMED (movie files were not probed;
  only the filenames at `0x315FC4`–`0x316050`, e.g. `s2c_intro_pal.xmv`, were observed in the
  guest image).

## 3. Render targets besides the back buffer

`D3DDevice_CreateTexture` / `D3DDevice_CreateRenderTarget` do not appear in
`config/game.toml` (`D3DDevice_CreateTexture` is not among the 132 located symbols, and no
`CreateRenderTarget` symbol exists for this XDK). Guest textures are created by guest XDK
code (surface headers built by `sub_002741F0` / `sub_00279000` in the `D3D` section) and by
the engine helpers below; the host renderer reads the resulting headers from guest memory
(`sdk/runtime/gpu/texture.cpp`).

| # | Target | Size | Creating function (guest) | Purpose | Must scale with internal resolution? |
|---|---|---|---|---|---|
| 1 | Back buffers + depth | Back-buffer size (640×480 on this host); depth format 46 (`LIN_D24S8`) | `Direct3D_CreateDevice` via `sub_001FB080` at `0x1FB1B8`; host creates them in `sdk/runtime/xdk/d3d8.cpp:374-378` | Main frame | Yes — this is the frame target. The host already renders it at render scale while keeping the guest extent (`sdk/runtime/gpu/d3d12.cpp:1187-1190`). |
| 2 | Post downsample texture 0 | BackBufferWidth/2 × BackBufferHeight/2 (320×240 in mode 0) | `sub_001F5530`, call to `sub_00270B60` at `0x1F55B3` (format push `0x1E`=30 at `0x1F55A5`) | Bloom / motion-blur pyramid level 0 (chain driven by `sub_001F56F0`, composited by `sub_001F5B50` / `sub_001F5F60`) | Proportionally with the back buffer. It is sized from the back-buffer descriptor at creation, so a larger guest frame automatically enlarges it; at host render scale the host target behind it grows (`d3d12.cpp:1189-1190`) while its guest extent stays 320×240. It must not be given a fixed 4K size independent of the chain's other levels. |
| 3 | Post downsample texture 1 | BackBufferWidth/4 × BackBufferHeight/4 (160×120) | `sub_001F5530`, call at `0x1F55EB` (shifts at `0x1F55E3`) | Pyramid level 1 | Same as #2. |
| 4 | Post downsample texture 2 | BackBufferWidth/8 × BackBufferHeight/8 (80×60) | `sub_001F5530`, call at `0x1F561F` (shifts at `0x1F5617`) | Pyramid level 2 | Same as #2. |
| 5 | Post downsample texture 3 | BackBufferWidth/16 × BackBufferHeight/16 (40×30) | `sub_001F5530`, call at `0x1F5653` (shifts at `0x1F564B`) | Pyramid level 3 | Same as #2. |
| 6 | Generic render-to-texture colour surface | Caller-supplied width×height (config struct copied at `0x214CD0`; width/height are `src[0]`/`src[1]`, forwarded at `0x214CE6`–`0x214CF3`) | Colour texture created in `sub_00214E70` (`0x214E70`) via texture object from `sub_00214B50` (`0x214B50`), descriptor call at texture vtable `+56`, surface obtained with `sub_00270C20` and stored at object `+44` | Generic off-screen pass (shadow / portrait / reflection class of object; the specific game users were not traced — UNCONFIRMED per instance) | Only if a given instance is screen-sized. A shadow-map-type instance must keep its authored size; a screen-sized instance must follow the frame. UNCONFIRMED which instances exist. |
| 7 | Generic render-to-texture depth surface | Same width×height as #6 | Created by `sub_00274220` (surface allocator in the `D3D` section) called from `sub_00214D30` (`0x214D30`), format 46 (`LIN_D24S8`) or 48 (`LIN_D16`) selected at `0x214D30` by object field `+28`; enabled by config flag `src[2]` passed at `0x214D03`–`0x214D0B` | Depth for #6 | Must match #6 exactly, whatever #6 does. |
| 8 | Guest XDK internal back-buffer surfaces | Back-buffer size, allocated inside guest XDK `sub_00277C10` (calls to `sub_002741F0` at `0x277F16`, `0x27800F`, `0x2780C9`) | Guest XDK only | Console-side device internals | Not applicable on this host: `Direct3D_CreateDevice` is replaced by the SDK (`sdk/runtime/xdk/d3d8.cpp:347-420`), so these guest allocations never happen. Listed to prevent double-counting them as engine targets. |

Notes on the table:

- The post chain (#2–#5) is created **once**, during Renderer initialisation: `sub_001F5530`
  is called at `0x1FB392` in `sub_001FB1CD`, right after device creation, and reads the back
  buffer's size through `D3DDevice_GetBackBuffer2` + surface descriptor `sub_002742B0` at
  `0x1F558A`–`0x1F5596`. Recreating the device (or changing mode) without recreating the
  chain leaves stale sizes.
- In the chain's first pass (`sub_001F56F0`), rendering switches with
  `D3DDevice_SetRenderTarget` at `0x1F56F0`-internal calls (texture surface, no depth), then
  restores the back buffer and depth at the end of the same function. The composite passes
  (`sub_001F5B50`, `sub_001F5F60`, called from the frame-effects dispatcher `sub_00201F70` at
  `0x201FAF`/`0x201FC2`, which also handles the motion-blur pass `sub_001F59D0`) bind all
  four levels as textures 0–3 and draw a fullscreen quad sized from the post object's stored
  width/height.
- No fixed-size shadow map, reflection buffer or blur buffer with literal dimensions was
  found besides #6/#7, whose size is a runtime parameter. A literal-size search over `.text`
  for functions containing both 640 and 480/448 immediates returned only `sub_001C33D0`
  (§1), the disc-error screen `sub_001F7EE0`, and two unrelated functions (`0x17ACE0`,
  `0x10C5E0`) that do not create render targets. The absence of other literal-size targets
  is therefore evidence-based, but the per-instance sizes of #6 remain UNCONFIRMED.

## 4. Resolution-dependent hazards for a 4K internal-resolution path

Each hazard is a place that assumes the guest's 640×480 / 640×448 pixel space. The safe
strategy, which the SDK already implements, is to keep every guest-visible size at its
console value and scale only the host resources behind them (guest extent vs. host pixels,
`sdk/runtime/gpu/d3d12.cpp:1169-1190`, `SetViewportScissor` at `d3d12.cpp:2554-2568`).

1. **Hard-coded 640×448 UI space.** Reciprocals `1/640` (`0x316478`) and `1/448` (`0x316474`)
   are baked into UI coordinate conversion at `0x168B7`/`0x168CC` (`sub_00016770`) and
   `0x12EC11`/`0x12EC23` (`sub_0012EB30`); the screen-size getters `sub_00014DB0` /
   `sub_00014DC0` (`0x14DB0`/`0x14DC0`) return literal 640.0/448.0 (`0x3163F8`/`0x3163F4`).
   Changing the *guest* frame size (e.g. forcing mode 1/2) breaks UI layout; scaling must
   stay host-side.
2. **Centring offsets in guest pixels.** The screen-space offsets
   (`display[+20] + 0.0/20.0/64.0 + 0.53125`, `display[+24] + 16.0/36.0 + 0.53125`) are
   computed at `0x1FD735`–`0x1FD75F` and stored at `Renderer+0x880`/`+0x884`, then uploaded
   as vertex-shader screen constants (constant 121 in `sub_001FB3F0`, constant 165 in
   `sub_001F56F0`/`sub_001F5B50`/`sub_001F59D0`). They are in guest pixels: the host must
   fold them through the guest extent (as `CentreVsConstant`, `sdk/runtime/xdk/d3d8.cpp:247`,
   and `UpdateViewportConstants`, `d3d8.cpp:271-278`, do) and never rewrite them in host
   pixels, or the picture shifts by the render scale.
3. **Scissor rects in guest pixels.** `sub_001FD6D0` builds scissor corners in guest pixels
   (adds at `0x1FD767`–`0x1FD7B2`, clamp against config width/height from `0x1C3320` /
   `0x1C3330`) and calls `D3DDevice_SetScissors` at `0x1FD931`. The host scales guest
   scissors by the target's scale in `SetViewportScissor` (`d3d12.cpp:2564-2568`); any new
   host draw path must do the same or clips will be wrong by the render-scale factor.
4. **Back-buffer read-back with a guest-pixel rect.** `sub_002019A0` copies a rect out of the
   back buffer into a texture surface with `D3DDevice_CopyRects` at `0x201A6F`; the rect
   (`a2[3..6]`) is in guest back-buffer pixels and is clamped against the destination
   texture's descriptor (`sub_002742B0` result) earlier in the same function. At render
   scale the host copy must interpret the rect in guest coordinates and downsample from the
   scaled frame (the host's `ScaledRect` path, `d3d12.cpp:2729-2731`, does this); a raw
   host-pixel copy would read the wrong region.
5. **Post chain created once, sized from the guest back buffer.** Textures #2–#5 are created
   at `0x1FB392` → `sub_001F5530` from the back-buffer descriptor, and the passes draw quads
   whose sizes are the stored guest width/height (`sub_001F56F0` halves them per level;
   composite quad in `sub_001F5B50`/`sub_001F5F60` via `sub_001F5390` with `width/16`,
   `height/16` terms). If the guest mode ever changes without a device recreation, the chain
   is stale. Whether the chain's pixel shaders use hard-coded texel steps (1/width style) is
   UNCONFIRMED — their constants are built from effect data at `0x1F5B50` (`v5 = a1[4] /
   (4 − *a1)`) and were not decoded; if they do, they remain correct only because the host
   keeps the guest extent of these targets unchanged.
6. **Generic render-to-texture depth (#7) at caller size.** `sub_00214D30` creates a depth
   surface at exactly the colour target's guest size (format 46/48). The host currently
   scales *every* target by the render scale (`d3d12.cpp:1189-1190`). For a shadow-type
   instance that only wastes fill; for a screen-sized instance it is required. The instance
   sizes are UNCONFIRMED (§3), so this cannot be decided per-instance from static evidence
   alone — first frame capture should check which sizes occur.
7. **Mode selection is host-kernel-dependent.** As shown in §1.3, the guest's mode choice at
   `0x260C02` depends on `HalBootSMCVideoMode`. The recommended `picture_top=16,
   picture_height=448` is valid for modes 0–1 only. Changing the host kernel to report SMC
   mode 1 (e.g. to enable 480p/720p) silently changes the guest frame to 720×480 or
   1280×720 and invalidates the vertical crop for mode 2 (top 36, height 648) and adds
   side bars in mode 1 that row-cropping cannot remove.
8. **Presentation interval is guest-specified but host-ignored.** The game requests interval
   2 (store at `0x1FB1AE`). The SDK's `Direct3D_CreateDevice` reads the interval
   (`sdk/runtime/xdk/d3d8.cpp:361`) but only logs it (`d3d8.cpp:366-367`); pacing is the
   host's. Any future host change that honours the guest interval would double-apply frame
   pacing against the host's own cap. (Effect on the current host: none — confirmed from
   the host source above, not from running the game.)

## 5. Summary

- On this host the game creates a **640×480, `LIN_X8R8G8B8`, single-buffered device with a
  `LIN_D24S8` depth buffer, no multi-sampling, swap effect 1, refresh 0 and presentation
  interval 2** (§1, stores in `sub_001FB080`, config built in `sub_001C33D0`).
- It draws a **640×448 picture at rows 16..463** of that frame. Set
  `.picture_top = 16, .picture_height = 448` in `src/common/game_info.cpp`.
- The only engine-created off-screen colour targets are the **four-level post downsample
  chain sized relative to the back buffer** (`sub_001F5530`) and a **generic
  render-to-texture class with caller-supplied size** (`sub_00214E70` + depth in
  `sub_00214D30`), whose instances' sizes are UNCONFIRMED.
- All resolution-sensitive guest code works in the fixed 640×448 guest pixel space (§4);
  internal-resolution scaling to 4K must remain a host-side transform of the frame and
  target resources, exactly as the SDK's guest-extent mechanism already does.
