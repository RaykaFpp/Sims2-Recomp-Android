# Internal-resolution (up to 3840×2160) hazard plan — The Sims 2 (Xbox, USA)

Static analysis only: guest disassembly/pseudocode from `build/sims2_flat.exe` (guest VA = flat
address, base `0x10000`, via the `ida` tool), host source in `sdk/runtime/`, sibling evidence in
`D:/Urbz/docs/`. No build, no launch. Anything not directly observed is marked UNCONFIRMED.
Starting point is the hazard list in `docs/analysis-video-mode.md` §3–§4; every item below was
re-verified independently, and two items left UNCONFIRMED there are narrowed (H5, H6).

## 0. The strategy in one paragraph

Do **not** change anything the guest can see. The guest frame stays 640×480 with a 640×448
picture (rows 16..463); every guest-visible size, rect, offset and reciprocal stays in guest
pixels. Resolution scaling is a purely host-side property of the D3D12 resources behind the
guest surface headers: at a 3840×2160 output with `render_percent = 100`, the host scale is
`2160/480 = 4.5`, so the frame target is 2880×2160 host texels behind a 640×480 guest extent,
and it is presented (cropped to the 640×448 picture) by `BlitFrame`. The SDK already implements
almost all of this (§1). The remaining work is: prove it per hazard with captures (§4, steps
1–2), enumerate the one class of target whose sizes are runtime data (H6, step 3), and fix only
what the captures actually show to be broken (steps 4–6 are conditional).

## 1. How the SDK renderer already scales (verified, with file:line)

| Mechanism | Where | Behaviour |
|---|---|---|
| Scale factor | `sdk/runtime/app/settings.cpp:190-193` (`RenderScale`: `Output().height * render_percent/100 / guest_height`), latched in `sdk/runtime/gpu/d3d12.cpp:1039` (`LoadSettings`) and re-read in `ApplySettings` (`d3d12.cpp:3595`) | One scalar for the whole renderer, derived from guest **height** only. 2160-line output ⇒ 4.5×. |
| Guest extent kept | `d3d12.cpp:885-886` (`guest_w_/guest_h_` set once in `Init`), `Target.gw/gh` set from the guest surface at `d3d12.cpp:1280-1281` | Guest-visible sizes never change. All guest math (rects, scissors, shader constants) stays valid. |
| **Every** render target scaled | `CreateTargetResource`, `d3d12.cpp:1182-1190`: `t.w = round(gw*sc)`, `t.h = round(gh*sc)` for colour **and** depth, of **any** guest size — back buffer, post chain, generic RTT, small depth surfaces alike. Only the frame target additionally gets a widened guest width for ultrawide (`gw = WidenedGuestWidth()`, `t.dx` centre offset, `d3d12.cpp:1187-1188`). | There is currently **no** per-target opt-out. A 128×128 shadow-type target at 4.5× becomes 576×576 host texels (wasted fill, sharper shadows — not a correctness defect by itself). |
| Settings change | `ApplySettings`, `d3d12.cpp:3594-3606`: on scale/widen change it flushes, returns all views, **clears `targets_`** and recreates lazily at the new scale. | Guest-created targets (post chain, RTT) are recreated host-side at the new scale automatically; their guest headers/sizes are untouched, so the guest's one-time creation (H5) is not a staleness problem on this host. |
| Viewport/scissor | `SetViewportScissor`, `d3d12.cpp:2554-2579`: viewport = full host target; guest viewport and guest scissor rects are multiplied by `W/size.w` (host/guest ratio), with the widen centre offset folded in. | Guest-pixel scissors and viewports are interpreted in guest space. Correct by construction. |
| Half-texel correction | `FillTexelConstants`, `d3d12.cpp:2234-2249`: for stages sampling a render target, `texsize.zw = -0.5/(d.width*k)`, `-0.5/(d.height*k)` where `k` folds the target's host/guest ratio; plus the `wide[]` box-spread compensation (`kGlowBoxTexels`, `d3d12.cpp:2242-2248`, set in `BindTargetTexture`, `d3d12.cpp:1761-1767`) so a downsample tap averages a 3-console-texel box instead of skipping texels at high host/guest ratios ("else upscaled taps skip texels and ghost", `d3d12.cpp:1761`). | This is exactly the machinery the post chain (H5) needs; it already exists. |
| Ordinary textures **not** scaled | `CreateTexture`, `d3d12.cpp:1784-1803`: host texture is created at `d.width × d.height` from the guest descriptor. | Texture assets keep authored resolution; only filtering/aniso changes their look. Correct — do not "fix". |
| Read-back to guest memory | `Sync`, `d3d12.cpp:3630-3653`, via `PrepareCopySource` (`d3d12.cpp:3610-3620`): a scaled target is first `Shrink`-ed to `gw × gh`, then read back and copied at guest pitch. `ReadBackFits` (`d3d12.cpp:349-…`) refuses a read-back that would overflow the guest surface. | CPU locks of drawn surfaces return guest-size pixels. Correct by construction. |
| `CopyRects` | `Copy`, `d3d12.cpp:2942-2975`: if the source was never drawn, the copy happens in guest memory (`CopySurfaceMemory`, `sdk/runtime/gpu/texture.cpp:441`); if drawn, GPU path — `CopyByRegion` scales guest rect coords by `dst_scale = dst.w/dst.gw` (`d3d12.cpp:2912,2921-2931`), `CopyByDraw` (format mismatch) builds UVs from guest extents and viewports from `dst_scale` (`d3d12.cpp:2877-2899`). Entry from HLE at `sdk/runtime/xdk/d3d8.cpp:685-689`. | Guest rects are guest pixels on both paths. See H4. |
| Small depth surfaces | `SizedTargetKey`, `d3d12.cpp:1239-1245` (commit `6e89e23`, §3): a depth surface smaller than the frame gets its own `targets_` entry keyed by guest size, instead of aliasing the frame depth target. | It keeps its own **target object**, not its own host resolution — `CreateTargetResource` still scales it by `scale_`. Also `BindTargets` drops a depth target whose host size differs from the colour target's (`d3d12.cpp:1298`), which cannot trigger while both are scaled by the same factor. |
| Present | `BlitFrame`, `d3d12.cpp:3388-3409`: draws the frame target into the swap chain with UV rect restricted to `picture_top/picture_height` rows (`app::kGame`, set to 16/448 in `src/common/game_info.cpp:45-46`). | The 2D/UI pass is rendered into the scaled frame in guest space and scaled **only here**, once. See H1. |
| Ultrawide centre fold (not resolution) | `sdk/runtime/xdk/d3d8.cpp:226-257` (`FrameCentreShift`, `CentreVsConstant` on VS constants 98/121), `d3d8.cpp:275-294` (`UpdateViewportConstants`). | Only active when `AspectWiden() > 1`; at widen = 1 it is a no-op and pure render scaling does not touch guest constants. |

**Summary of what is scaled today:** host resources behind *all* guest render/depth targets
(uniform `scale_`), nothing else. Guest extents, guest memory, ordinary textures, and all
guest-supplied numbers are unscaled.

## 2. Hazard-by-hazard verification and decision

### H1 — Hard-coded 640×448 UI space — VERIFIED — keep guest-side, scale only at present

Evidence (re-read in IDA): the floats at `0x316478` and `0x316474` decode to `1/640`
(0.0015625) and `1/448` (0.0022321428); they are multiplied into UI coordinates at
`0x168B7`/`0x168CC` (in `sub_0001676D`: `movss xmm0,[ecx+30h]; mulss xmm0,dword_316478` …
`movss xmm0,[edx+34h]; mulss xmm0,dword_316474`) and at `0x12EC11`/`0x12EC23` in
`sub_0012EB30`. The getters `sub_00014DB0`/`sub_00014DC0` return the floats at
`0x3163F8`/`0x3163F4` = 640.0/448.0. The 2D screen matrix is built from the *picture* size
(`display+12`/`display+16`) in `sub_001FB3F0` and uploaded as VS constants 121..124
(verified: matrix rows at `Renderer+1776`/`+1796` set from `(float)*(int*)(display+12/16)`,
then `D3DDevice_SetVertexShaderConstant4(121, …)`).

Decision: **keep at guest resolution deliberately; scale only at present.** This is already
the architecture: UI is drawn into the scaled frame target through the guest screen matrix,
and `BlitFrame` (`d3d12.cpp:3388-3409`) does the one and only upscale, cropped to rows
16..463. Any attempt to "fix" the reciprocals or force a larger guest mode breaks layout
for zero benefit. The only real risk at 4.5× is sampling artefacts on the UI draws
themselves — covered by the Urbz clamp fix (§3) — not geometry.

### H2 — Centring offsets in guest pixels — VERIFIED — let the host fold them (already does)

Evidence: `sub_001FD6D0` computes `offsetX = (float)display[+20] + display_float[+728] +
0.53125` at `0x1FD735-0x1FD746` (and Y at `0x1FD74E-0x1FD75F`; `0x316418` decodes to
0.53125), stores them at `Renderer+0x880/+0x884`, and the same computation appears in
`sub_001FB080` (`0x1FB0F9-0x1FB12E`). Mode 0 gives (0.0, 16.0) — exactly
`(640-640)/2, (480-448)/2`. These offsets reach shaders as VS constants (121 in
`sub_001FB3F0`, 165 in the post passes).

Decision: **host folds, guest value untouched.** At pure render scale the fold is the
guest-extent mechanism itself: `BoundExtent` (`d3d12.cpp:1170-1179`) reports the guest
extent to the shader fold while the rasteriser covers the host target
(`SetViewportScissor`, `d3d12.cpp:2555-2559`), so a guest-pixel offset lands on the right
host texels automatically. The explicit constant rewrite (`CentreVsConstant`,
`d3d8.cpp:250-257`) fires only for the ultrawide centre shift (`FrameCentreShift`,
`d3d8.cpp:226-232`, zero when widen = 1). Do not rewrite the guest constants in host
pixels — that would shift the picture by `scale_`.

### H3 — Scissor rects in guest pixels — VERIFIED — host scales (already does)

Evidence: in `sub_001FD6D0`, after the offset adds at `0x1FD767-0x1FD7B2`, the corners are
clamped against the back-buffer size from display vtable slots 40/41 (`call [eax+0A0h]` /
`[eax+0A4h]` at `0x1FD7D4`/`0x1FD7FB`) and passed to `D3DDevice_SetScissors` at
`0x1FD931`. In the off-screen branch (`0x1FD820`) the offsets are forced to 0 and the
clamp uses the bound target's own size getters — confirming the offset/clamp are
frame-centring only.

Decision: **host scales.** `SetViewportScissor` (`d3d12.cpp:2564-2569`) intersects the
guest viewport with the guest scissors in guest units, then multiplies by the target's
host/guest ratio. Any *new* host draw path (e.g. a future blit) must go through
`ScaledRect`/`SetViewportScissor`, never raw guest rects.

### H4 — Back-buffer read-back via `CopyRects` at `0x201A6F` — VERIFIED — host downsamples; never scale the guest rect

Evidence: `sub_002019A0` builds the source rect from `a2[3..6]`, the destination point
from `a2[6..7]`, obtains the destination texture's surface via vtable+68 →
`sub_00270C20`, reads its descriptor with `sub_002742B0`, clamps the rect against the
destination size (`0x201A0D-0x201A59`, forcing at least 1×1), then calls
`D3DDevice_CopyRects(backbuffer, &rect, 1, dst_surface, &point)` at `0x201A6F`
(verified in asm: pushes at `0x201A61-0x201A6E`, `call D3DDevice_CopyRects` at
`0x201A6F`). So the rect is in guest back-buffer pixels and the destination is a
guest-sized texture surface whose pixels the guest subsequently uses as a texture.

Decision: **special-case by construction — the copy must produce guest-size pixels.**
This is a read-back class consumer: it cannot consume a 4.5× image. The host already
honours that: `Copy` (`d3d12.cpp:2942-2975`) either copies in guest memory or scales the
guest rect by `dst_scale` on the GPU path (`CopyByRegion`, `d3d12.cpp:2921-2931`;
`CopyByDraw`, `d3d12.cpp:2887-2899`), so the destination target behind the guest texture
holds the correctly downsampled-by-construction image at its own (scaled) host size,
and any later CPU lock returns `gw × gh` pixels via `Sync` (`d3d12.cpp:3645-3649`).
Residual risk to check in a capture (step 5): `CopyByRegion` uses
`ScaleUnsigned(r.left, dst_scale)` for the *source* box too (`d3d12.cpp:2921-2924`) —
i.e. source offsets are scaled by the destination's scale, which is only right when
`src_scale == dst_scale`. For this call site source = frame (scale 4.5 at 2160p) and
destination = a texture of unknown guest size (also scaled 4.5), so the scales match
today; if a future change gave targets different scales, this is the first place that
breaks. Falsifiable check in step 5.

### H5 — Post-process downsample chain, created once in `sub_001F5530` — VERIFIED, and the texel-step question narrowed

Evidence: `sub_001FB1CD` calls `sub_001F5530(this+613)` once at `0x1FB392`, right after
device creation and the first two swaps. `sub_001F5530` reads the back-buffer size via
`D3DDevice_GetBackBuffer2` + `sub_002742B0`, then creates four textures (format 30 =
`LIN_X8R8G8B8`, pushed at each call) at `>>1, >>2, >>3, >>4` of that size
(320×240, 160×120, 80×60, 40×30 in mode 0), stored at post-object `+5..+8`. The passes:
`sub_001F56F0` downsamples level by level (each `sub_001F5450` quad halves the stored
size), and `sub_001F5B50`/`sub_001F5F60` composite with all four levels bound as stages
0–3 and a fullscreen quad from `sub_001F5390` sized with `width/16, height/16` terms.
The stored width/height (`+12/+13`) are recomputed **per frame** in `sub_001F4D50` from
the renderer's current size fields (`a2[12] = a1[0]*2.0`, `a2[13] = a1[1]*2.0`), so they
track the guest frame, not a stale creation-time value.

Texel steps — the part left UNCONFIRMED in `analysis-video-mode.md`: the pixel-shader
constants the guest actually uploads are **not** 1/size values. In `sub_001F56F0` and
`sub_001F5B50`, PS constant 0 is `{0.31, 0.58, 0.11, aspect}` — the floats at
`0x316414`/`0x316410`/`0x3163C4` decode to 0.31/0.58/0.11 (luminance weights) — and
constants 1–4 are the scalar `a1[4]/(4-*a1)` (a per-level weight). The quad builders
(`sub_001F5450`, `sub_001F5390`) emit positions and UVs in **guest pixel units**
(e.g. UV corners `(a3+a5, a4)`, `(0,0)`, width/height terms), which the host shader
fold divides by the texture size it is told (`texsize`, `d3d12.cpp:2235-2241`). So the
chain's sampling is resolution-relative by construction, and the host's `-0.5/(size*k)`
half-texel offset plus the `wide[]` 3-texel box (`d3d12.cpp:2240-2248, 1761-1767`) are
precisely the compensation it needs at 4.5×. What remains UNCONFIRMED: constants
embedded *inside* the Xbox shader microcode blobs (`dword_33A460` vertex shader,
`dword_33A518/33A608/33A6F8/33A7E8` pixel shaders, created at the top of
`sub_001F5530`) were not decoded — if any tap offset is baked there as a guest-texel
fraction, it would be wrong at host scale in a way constant uploads cannot reveal.
That can only be settled by the A/B capture in step 4.

Decision: **let the host scale the whole chain proportionally** (it does:
`CreateTargetResource` scales each level by the same `scale_`, preserving the
1:2:4:8:16 ratios). Never give the chain a fixed host size. The one-time creation is
safe because the host recreates target resources on settings change
(`ApplySettings`, `d3d12.cpp:3598-3606`) while the guest headers keep their guest
sizes; the guest only re-creates the chain on device recreation, which rebuilds the
host device too. Do not touch guest code.

### H6 — Generic render-to-texture class `sub_00214E70` / `sub_00214D30` — sizes remain runtime data (narrowed)

Evidence: the class (vtable `off_315C14`, name string "base test" at `0x315C08`) is
constructed only by the factory `sub_001EA090` (allocates 56 bytes, calls the ctor
`sub_214BB0`), which is referenced only from two engine class-registry tables
(`0x313998`, `0x314550`) — i.e. instances are created by the engine's generic
object/property system, not by direct C++ call sites. The ctor sets width/height
fields (`this+4/+8`) to −1 (`sub_214BB0`), and the configure method `sub_00214CD0`
(vtable+12) copies a 32-byte config `{src[0]=width, src[1]=height, src[2]=flags, …}`
into the object and calls `sub_00214E70(width…, height…)`; `sub_00214E70` stores them
at `a1[1]/a1[2]`, builds a texture object (`sub_00214B50`), creates the colour texture
through texture-vtable+56 with descriptor `{w=(int16)a3, h=(int16)a4, fmt byte 32 or
16 depending on a flag}` and takes its surface via `sub_00270C20` into `+44`.
`sub_00214D30` (vtable+0x2C… entry at `0x315C2C`) creates the matching depth surface
via `sub_00274220(w, h, 2, fmt)` with format 46 (`LIN_D24S8`) or 48 (`LIN_D16`)
selected by object field `+28`, only when config flag bit 0 is set. Binding is
`sub_00214F80`: `D3DDevice_SetRenderTarget(colour_surface+44, depth+40)` plus a
one-time clear. **No literal width/height for any instance exists in the image** —
the config is copied from a caller-supplied struct at runtime (scene/asset data),
so per-instance sizes are UNCONFIRMED from static evidence alone, and no amount of
further disassembly of these three functions will resolve them. They are resolved by
the logging hook in step 3, at runtime, in one run.

Decision: **default to host scaling (current behaviour), then special-case only if
step 3 shows an instance that must not scale.** Reasoning: every plausible instance
(shadow, portrait, reflection, screen overlay) is *rendered and then sampled as a
texture*; scaling its host resource preserves all guest-visible semantics (the shader
fold uses the guest extent, sampling uses `texsize` ratios), at the cost of fill.
The two failure modes that would force an opt-out are (a) a consumer that locks/CPU-
reads the surface expecting authored texel counts — then `Sync`'s guest-size shrink
(`d3d12.cpp:3613-3615, 3645`) already returns the right count, so still safe — and
(b) a fixed-size *depth* instance whose host size must match a *colour* target of a
different guest size; `BindTargets` (`d3d12.cpp:1298`) drops mismatched depth, which
is the symptom to watch for in the step-3 capture. Until an instance is observed,
adding a per-size exemption list would be guessing.

### H7 — Guest reads pixels back in general (locks, `Sync` paths) — VERIFIED host-side

Any guest lock of a drawn surface funnels to `Sync`/`SyncLate`
(`d3d12.cpp:3630-3660`), which downsamples to `gw × gh` before writing guest memory,
and `LatchCpuWrite`/`UploadTarget` (`d3d12.cpp:425-…, 1438-…`) push CPU-written pixels
back up scaled. Decision: **never scale these surfaces' guest side**; the invariant
"guest memory always holds guest-size pixels" is already maintained. New code must
not add a path that copies host-size pixels into guest memory.

### H8 — Mode selection and presentation parameters — VERIFIED, out of scope but load-bearing

The guest always selects mode 0 (640×480) on this host (host `HalBootSMCVideoMode`
returns 0, masking modes 1–2 at guest `0x260C02`; see `analysis-video-mode.md` §1.3),
and `picture_top=16, picture_height=448` is already set (`src/common/game_info.cpp:
45-46`). Presentation interval 2 is guest-requested and host-ignored. Decision:
**do not** enable guest modes 1/2 or honour the guest interval as a way to get
resolution — that changes the guest frame the UI is authored for (H1) and invalidates
the crop. Internal resolution comes from `render_percent`/output height only.

### Half-texel / 1-over-size constants — sweep result

Grepped the guest float table (`0x316240-0x316480`) and the post-chain setup: the
only resolution reciprocals present are `1/640` (`0x316478`), `1/448` (`0x316474`)
(H1, UI space — must stay), `1/4096` (`0x316264`, used as a VS constant component in
`sub_001FB3F0`, a z-scale class constant, not a texel step), and the luminance/weight
constants of H5. No `1/320`, `1/224`, `1/240`, `1/480` or `0.5/640`-style texel-step
immediates feed the post chain's *uploaded* constants. Guest quads carry UVs in guest
pixel units (H5), so the effective texel step is computed host-side from `texsize`
(`d3d12.cpp:2235-2241`) and scales correctly. Residual UNCONFIRMED: tap offsets baked
into the undecoded Xbox shader microcode (H5) — settled empirically in step 4, not by
more static reading.

## 3. What the sibling (Urbz) fixes already cover here

Both named SDK commits are already in this SDK's history (`git -C D:/sims2/sdk log
--oneline` shows `07c8c36` and `6e89e23`) and their code is present in the current
`d3d12.cpp`:

- **`07c8c36` "Clamp wrapped textures on HUD draws at high resolutions"** — present as
  `UnitTexcoords` (`d3d12.cpp:2516-…`) feeding `EnsureStageSamplers(…, clamp)`
  (`d3d12.cpp:2549`) and the wrap→clamp sampler rewrite (`d3d12.cpp:2008-2010`). It
  covers the H1-class defect at high scale: a 0..1-UV HUD/UI quad whose far edge would
  otherwise wrap-blend the opposite edge texel (thin dark lines on sprites). It does
  **not** cover: post-chain tap spread (that is the separate `wide[]` box,
  `d3d12.cpp:2242-2248`), CopyRects (H4), or RTT instances (H6).
- **`6e89e23` "small depth surfaces keep their own target"** — present as
  `SizedTargetKey` (`d3d12.cpp:1239-1245`) and its uses in `DepthCopy`/texture bind.
  It covers H6's depth-aliasing half: a small depth surface (e.g. an RTT depth at
  caller size, H6) no longer aliases the frame's depth target. It does **not** keep
  the small depth surface at guest *host* size — it is still scaled by `scale_` in
  `CreateTargetResource` — and it does not decide whether any RTT instance *should*
  be exempt from scaling (step 3/6).
- Urbz's ultrawide Hor+ work (`D:/Urbz/docs/status.md`, "Settings overlay,
  ultrawide…": widening moves the viewport offset, never its scale; "only the main
  frame target grows, so off-screen passes keep their own size and centre") is the
  `widen_`/`t.dx` mechanism above (`d3d12.cpp:1161-1165, 1187-1188`). It is orthogonal
  to render scale and already correct for this title; Sims 2 inherits it unchanged.
- Urbz's draw-path findings (`D:/Urbz/docs/input-and-draw-path.md` §3) concern the
  raw push-buffer constant/vertex packets, not resolution; nothing there changes any
  decision above. Noted only so nobody re-derives it.

## 4. Ordered implementation plan

Each step names the file, the change, and the falsifiable check. Steps 1–3 are
observation; do not skip to 4–6 without their captures. "Capture" means a run with
`XBE_CAPTURE_DIR` set and F3 pressed (`sdk/runtime/core/diag.h:15-26`); the capture
writes `frame.txt` plus per-target BMPs (shrunk to guest size, `d3d12.cpp:3844-3847`)
into a timestamped folder. Probe means additionally `XBE_CAPTURE_PROBE=x,y`
(fractions of the *presented picture*, `diag.h:28-…`; probe pixel mapping at
`d3d12.cpp:3232-3238`).

1. **Baseline at scale 1, then at 4.5× — no code change.**
   Run the title at a 640×480-class output (or `render_percent` chosen so
   `RenderScale = 1`) and at 3840×2160/100%. Capture a frame with UI on screen
   (title/menu) and a frame in-game at both scales.
   *Check:* in each capture's `frame.txt`, every target's guest size is the console
   size (frame 640×480, post levels 320×240/160×120/80×60/40×30) while the log line
   `d3d12: %ux%u guest, … render scale %.3f` (`d3d12.cpp:903`) shows 1.000 vs 4.500.
   The presented frame at 4.5× must show the UI in the same positions as at 1×
   (compare the frame BMPs) — any shift falsifies H1/H2/H3's "already correct" and
   stops the plan here.

2. **Verify the present crop and UI edge behaviour at 4.5× — no code change.**
   With `XBE_CAPTURE_PROBE` on a known UI edge pixel (e.g. the right edge of a menu
   panel, approx `0.98,0.5` of the picture), capture at 2160p.
   *Check:* `frame.txt`'s probe section lists the draws that changed that pixel; the
   final colour must equal the same probe at scale 1. A dark/fringe value at the
   edge means the `07c8c36` clamp is not firing for this title's UI draws
   (`UnitTexcoords` rejects draws with >4096 vertices or non-unit UVs,
   `d3d12.cpp:2520`) — that, and only that, triggers step 4.

3. **Enumerate the generic RTT instances (H6) — add logging only.**
   File: `src/game/hooks.cpp` — add a host hook for guest `sub_00214CD0`
   (configure) that logs `src[0], src[1], src[2]` (width, height, flags) and the
   caller, following the existing hook style in that file; or, if a hook is
   impractical, log in `sdk/runtime/xdk/d3d8.cpp` where the RTT colour/depth
   surfaces are first bound as render targets with their guest sizes. Run through
   menus, Create-a-Sim, and a lot with shadows/reflections; collect every distinct
   `(w,h,flags)`.
   *Check:* the log either (a) shows only screen-relative sizes (640×480,
   320×240, …) — then host scaling is right for every instance and H6 closes with
   no further change; or (b) shows fixed authored sizes (e.g. 128×128, 256×256) —
   record them verbatim for step 6. No instance observed at all after exercising
   CAS/shadows also closes H6 (the class is then unused by shipped content, like
   its "base test" name suggests). This step is what converts H6 from UNCONFIRMED
   to decided; the document must be updated with the observed list.

4. **(Conditional, from step 2) Extend the HUD clamp to this title's UI draws.**
   File: `sdk/runtime/gpu/d3d12.cpp` — only if step 2 shows edge fringing: inspect
   why `UnitTexcoords` (`d3d12.cpp:2516-…`) rejected the draw (vertex count, UV
   range, or pre-transformed quad path) and widen the predicate for that shape, or
   clamp the specific stage in `StageSampler` (`d3d12.cpp:2008-2010`).
   *Check:* repeat the step-2 probe; edge pixel matches scale 1, and a wrapped
   world texture (e.g. terrain) in the same capture still wraps (no clamp leak into
   3D draws — compare a terrain BMP against the pre-change capture).

5. **Verify the `CopyRects` read-back (H4) at 4.5× — no code change unless falsified.**
   Trigger the guest path (`sub_002019A0`; it runs on renderer command dispatch —
   in practice, capture frames until `frame.txt` contains a `copy … target to …`
   line, `d3d12.cpp:2946-2949`). Compare the destination target's BMP at scale 1
   and 4.5×: same guest-size image, no offset/scale error.
   *Check:* if the 4.5× copy is shifted or zoomed, the bug is `CopyByRegion`
   scaling source coords by `dst_scale` (`d3d12.cpp:2921-2924`); fix in
   `sdk/runtime/gpu/d3d12.cpp` by scaling the source box by
   `src_t.w/src_t.gw` and the destination point by `dst_scale` independently, then
   re-run: destination BMP identical to scale 1. If it already matches, change
   nothing.

6. **(Conditional, from step 3b) Exempt fixed-size RTT instances from scaling.**
   File: `sdk/runtime/gpu/d3d12.cpp` — only for sizes step 3 proved to be authored
   fixed-size *and* shown defective scaled (the realistic defect is depth dropped
   by `BindTargets`' size match, `d3d12.cpp:1298`, or a consumer reading texel-
   exact data): add a size-based exemption in `CreateTargetResource`
   (`d3d12.cpp:1183-1190`) so those guest sizes get `sc = 1` for colour **and**
   their paired depth (they must stay equal, or `BindTargets` drops the depth),
   keyed by the exact `(gw,gh)` list from step 3 — never a blanket "small targets
   don't scale" rule, which would un-scale the post chain's lower levels.
   *Check:* capture the scene that uses the instance (from step 3's log context):
   the instance's target BMP is now guest-size in `frame.txt`'s listing, the effect
   it feeds (shadow/portrait/reflection) renders, and the post chain levels are
   still scaled (their BMPs unchanged from step 1).

7. **Post-chain ghosting A/B (settles the H5 UNCONFIRMED).**
   Capture an in-game frame with the post effect active (bloom/motion blur —
   dispatcher cases 2/3 in `sub_00201F70`) at scale 1 and 4.5×, probe a bright
   edge pixel with `XBE_CAPTURE_PROBE`. *Check:* glow width in presented pixels
   scales with output (≈4.5× the scale-1 width, same relative brightness). If
   instead the glow is 1 guest-texel wide at both scales (ghosting/thin halo),
   the tap offsets are baked into the shader microcode or the `wide[]` box is not
   engaging (`BindTargetTexture`, `d3d12.cpp:1763-1767` requires `box` and a
   smaller bound target): fix in `sdk/runtime/gpu/d3d12.cpp`'s
   `FillTexelConstants`/`BindTargetTexture` by ensuring the composite stages
   (stages 0–3 in `sub_001F5B50`) get `wide[]` set, then re-probe.

8. **Final regression pass — no code change.**
   Re-run steps 1–2 captures at 720p, 1080p, 1440p, 2160p outputs and after a
   live `render_percent` change in the F1 panel (exercises `ApplySettings`
   target recreation, `d3d12.cpp:3594-3606`, against the guest's one-time post
   chain). *Check:* no stale-size targets in `frame.txt` after the change, UI
   probe pixels identical at every scale, and the log shows exactly one
   `render scale` relatch per change. Also confirm `src/common/game_info.cpp`
   still says `.picture_top = 16, .picture_height = 448` — if anyone enabled a
   guest widescreen mode instead (H8), this crop is the first thing that breaks.

## 5. Decisions at a glance

| Hazard | Verdict | Action |
|---|---|---|
| H1 UI 640×448 space | VERIFIED | Keep guest; scale only in `BlitFrame`. No change. |
| H2 Centring offsets | VERIFIED | Host guest-extent fold already correct; never rewrite in host pixels. |
| H3 Guest-pixel scissors | VERIFIED | Host scales in `SetViewportScissor`. No change. |
| H4 `CopyRects` read-back @ `0x201A6F` | VERIFIED | Host guest-rect scaling already correct; verify (step 5), fix `CopyByRegion` source scale only if falsified. |
| H5 Post chain (once, `0x1FB392`) | VERIFIED; microcode constants UNCONFIRMED | Host scales proportionally; A/B capture (step 7) settles the rest. |
| H6 Generic RTT (`sub_00214E70`/`sub_00214D30`) | Sizes UNCONFIRMED — runtime config, no literals in image | Log instances (step 3); scale by default; exempt only proven fixed sizes (step 6). |
| H7 CPU read-backs/locks | VERIFIED | Guest memory always guest-size (`Sync` shrink). Preserve invariant. |
| H8 Guest video modes / interval | VERIFIED | Do not use guest modes for resolution; `render_percent` only. |
