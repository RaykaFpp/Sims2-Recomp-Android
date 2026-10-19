# XDK gap triage — The Sims 2 (USA), XDK 5849

Inputs: `build/missing_symbols.txt` (108 names), `config/game.toml` (132 of 240 located),
`sdk/xdk/5849/signatures.toml`, `build/sims2_flat.exe` (IDA), `D:/Urbz/config/game.toml`,
`D:/Urbz/docs/gap-analysis.md`. Section set of this XBE (from the XBE header):
`.text` 0x11000, `D3D` 0x270880, `XGRPH` 0x284080, `DSOUND` 0x2A5740, `XMV` 0x2AF920,
`XPP` 0x2D73C0, `.rdata` 0x2DED60, `.data` 0x31E020, `DOLBY`, `.XTLID` 0x401100 (0x568 bytes).
There is no `BINK`, `XNET` or `XONLINE` section. Matching is section-restricted
(`sdk/tools/symbols/main.cpp:480` via `D:/Urbz/docs/gap-analysis.md:28-30`), so signatures
recorded against an absent section can never match.

## 1. Crash path (priority): D3D init fault at guest 0x3214

Observed (`out/build/release/logs/sims2.log`, reported by the orchestrator): exception
C0000005, read of guest address `0x00003214`, registers `ecx = edi = 0x00283798`,
`edx = 0`, return addresses `0x27AD8D` (in `sub_0027AD76` +0x17), `0x277F1B` / `0x277F8C`
(in `sub_00277C10`), `0x2715A3` (in `sub_00271570`), then game code
`0x1FCB5F`, `0x1FC096`, `0x1E5969`, `0x21D005`.

### 1.1 The faulting instruction

`sub_0027AD76` (0x27AD76, 494 bytes) begins (`build/sims2_flat.exe:0x27AD76` asm):

```
0x27ad7e  mov  esi, [ecx]          ; esi = *this
0x27ad84  push eax                 ; &local
0x27ad88  call sub_0027AA61        ; return address 0x27AD8D = crash [esp+008]
```

`sub_0027AA61` (0x27AA61, 107 bytes) is a hardware-idle poll. Its pseudocode
(`build/sims2_flat.exe:0x27AA61`):

```c
v3 = *this;
while ( (*(_BYTE *)(v3 + 12820) & 0x10) == 0      // 12820 = 0x3214  <-- faulting read
     || (*(_BYTE *)(v3 + 9216)  & 0x10) == 0      // 0x2400
     || (*(_BYTE *)(v3 + 12832) & 0x10) != 0 )    // 0x3220
{
    sub_0027A3D0(this);
    if ( *(int *)((char *)&dword_400100 + v3) != 0 ) sub_0027A320(this);
    if ( (*(_DWORD *)(v3 + 256) & 0x1000000) != 0 ) sub_00279DB0(this);
}
*a2 = *(_DWORD *)(v3 + 12832);
```

So guest offset **0x3214 is read off `v3 = *this`**, i.e. `*(NULL + 0x3214)` because
`*this == 0` (`edx = 0` at the fault). 0x3214 / 0x2400 / 0x3220 are status-register
offsets inside the NV2A register window, relative to a hardware base pointer stored
in the first dword of the object `this` points at. That object is the D3D miniport:
at the fault `ecx = 0x00283798`, and

```
D3D_Device (config/game.toml [data])  0x281B70
+ miniport offset (sdk/xdk/5849/layout.toml:29, "the miniport is at +0x1C28")  0x1C28
= 0x283798   == ecx
```

`*0x283798` (CDevice+0x1C28, the miniport pointer) is 0 because the host
`Direct3D_CreateDevice` (`sdk/runtime/xdk/d3d8.cpp:347-420`) memsets the whole device
(`std::memset(HostPtr(kDevice), 0, XDK_CDevice_Size)`, line 368) and never writes
+0x1C28 — on the console the guest driver init allocates the miniport and maps the
NV2A registers there. The guest's own display-driver code then dereferences it.
This is not a push-buffer write: 0x3214 is a *status poll* offset (`& 0x10` test,
spin until idle), matching the parent note that this is the signature of console
driver code touching a hardware register window the host never built.

### 1.2 The functions on the path

Call graph verified by IDA xrefs in `build/sims2_flat.exe`:

```
game .text (… 0x1FCB5A, 0x1F803E)
  -> sub_00271570   (0x271570, 168 bytes, stdcall(present-params*))
       D3D_KickOffAndWaitForIdle(); spin on sub_00279B80;
       sub_00277AE0(device);                      // surface teardown
       sub_00277C10(device, params);              // display-mode set
       sub_002775A4(…); SetRenderTarget; Clear;
     sub_00277C10   (0x277C10, 1430 bytes, thiscall(device, params*))
       creates front/back/depth surfaces (sub_00278840, sub_00278950,
       sub_00279000, sub_002741F0), stores them at device+1669…+1699,
       then sub_00272120(0/1, &surface-desc)       // crash returns 0x277F1B / 0x277F8C
       sub_00272120 (0x272120, 209 bytes, stdcall(which, desc*))
       copies desc to device+1712+6*which, then
       sub_0027AD76(this: device+1802, …)          // only xref: 0x2721A3
         sub_0027AD76 (0x27AD76, 494 bytes, thiscall(miniport-ptr, …))
       programs NV2A surface/tile registers: writes at base+0x100300…,
       base+0xEA0000-masked methods, spins until register readback matches
       (*v27 != *v30 …), stores result at *(base+12832)
         -> sub_0027AA61 (0x27AA61)  *** FAULT ***
```

Identities (semantic, from the bodies above):

| Address | Length | Identity | Evidence |
| --- | --- | --- | --- |
| 0x271570 | 168 | Internal D3D display reset/init entry, called directly by game code | Xrefs from `.text` 0x1F803E, 0x1FCB5A; body sequences KickOffAndWaitForIdle → teardown 0x277AE0 → mode set 0x277C10 → 0x2775A4 → located `D3DDevice_SetRenderTarget` (0x271620) → located `D3DDevice_Clear` (0x274F80) |
| 0x277AE0 | 303 | Internal surface teardown | Releases device+1665/+1666 via `D3D_InternalRelease` (0x270FF0), device+1669… via 0x270E50, clears tiles via 0x272120(which, nullptr) |
| 0x277C10 | 1430 | Internal display-mode set ("the mode set") | Creates surfaces, stores at device+1669 (render target ptrs), +1672 (depth), +1697/+1698/+1699 (allocations); cf. `sdk/xdk/5849/layout.toml:31` "the mode set … stores the mode entry at miniport + 0x1B4" (signing-title address, same function family) |
| 0x272120 | 209 | Internal surface-tile setup | Stores 6-dword surface descriptor at device+1712+6*which; forwards to 0x27AD76 with `this = device+1802` (= &CDevice+0x1C28 miniport field, since device is `buf` = 0x281B70 and 0x281B70+1802*4 = 0x283798) |
| 0x27AD76 | 494 | Internal NV2A tile/register programmer | Method words `(4*a2+16) & 0x1FFC | 0xEA0000`, register writes at base+0x100300, base+1049156…, spin-on-readback loops, final store `*(v8 + 12832) = v19` |
| 0x27AA61 | 107 | Internal GPU-idle poll | Spin on `[base+0x3214] & 0x10`, `[base+0x2400] & 0x10`, `[base+0x3220] & 0x10`; calls 0x27A3D0 / 0x27A320 / 0x279DB0 while waiting |

**None of these six is an XDK `[symbols]` export, in either title.** Checks:

- `sdk/xdk/5849/signatures.toml` has no D3D signature of length 168, 209, 303, 494,
  613 or 1430 (full-file scan by `len`); the only length-0xB0 (176) D3D signature is
  the already-located `CDevice_KickOff` (0x2757C0, whose recorded constants include
  0x1C28 — the same miniport field this path dereferences).
- `D:/Urbz/config/game.toml` contains no name for the Urbz counterparts either:
  its `[functions]` table is empty and no `[symbols]` entry falls between
  `D3DDevice_InsertFence` (0x24AF10) and `D3DDevice_SetRenderTarget` (0x24B190),
  the interval where the Urbz analogue of 0x271570 sits. Urbz simply never names
  this internal family; its guest copies run as ordinary recompiled code.
- Consequently **there is no `name = 0xADDRESS` line that can be pasted into
  `config/game.toml` `[symbols]` for 0x271570 / 0x277C10 / 0x27AD76** — the SDK's
  symbol universe (240 names) does not contain them, and `xbe_symbols` only writes
  names from that universe. Naming them under `[functions]` would only add entry
  points; it would not replace the bodies with host code. The fix must be on the
  host side (e.g. make the host device setup leave a valid miniport stand-in at
  CDevice+0x1C28 whose +0x3214/+0x2400/+0x3220 status bits read as idle, or
  intercept this internal path), which is outside this triage. UNCONFIRMED: which
  host change the orchestrator will prefer.

### 1.3 Which listed D3D names are on this call path?

Of `D3DDevice_BeginPush`, `D3DDevice_EndPush`, `D3DDevice_SwitchTexture`,
`D3DDevice_SetPalette`, `D3DDevice_FlushVertexCache`,
`D3DResource_BlockUntilNotBusy`, `D3DDevice_PersistDisplay`,
`D3DDevice_EnableOverlay`, `D3DDevice_SetTile`, `D3DDevice_GetRenderTarget2`:

**None of them is called on this path.** The complete callee set of the path
(IDA `call` xrefs) is: `D3D_KickOffAndWaitForIdle` (0x275CB0, located),
0x279B80, 0x277AE0, 0x277C10, 0x2775A4, `D3DDevice_SetRenderTarget` (0x271620,
located), `D3DDevice_Clear` (0x274F80, located), 0x272120, 0x27AD76, 0x27AA61,
0x27A3D0, 0x27A320, 0x279DB0, plus surface helpers 0x278840 / 0x278950 / 0x279000 /
0x2741F0 / 0x2713B0 inside 0x277C10. No call targets 0x272020 (SetPalette, §4),
0x272FD0 (SetVertexShaderConstant1Fast, §4), or any other candidate in §5.
The nearest relative is `D3DDevice_SetTile`: 0x27AD76 is the hardware-level tile
programmer *underneath* the XDK's tile API (the recorded SetTile signature's second
callee is 209 bytes, exactly 0x272120's length — §5), but SetTile itself
(sig length 0xA) is not on the path; the mode set calls the internal 0x272120
directly.

## 2. Method and the .XTLID result

Per name, `sdk/xdk/5849/signatures.toml` records `sec`, `len`, the callee cluster
(`calls`) and, for some names, `xtlid`. The image's `.XTLID` section was decoded
directly from `assets/default.xbe` (raw offset 0x33D000, 0x568 bytes = 173
id/address pairs; first pair is `(0, 5849)`, the XDK build). Result:

- **No missing name is recoverable by XTLID.** Every missing name that carries an
  `xtlid` in the signature file (all XNET/XONLINE names, most DSOUND names,
  `D3DResource_BlockUntilNotBusy` 0x8006A, `XNetGetEthernetLinkStatus` 0x4003A)
  has that id **absent** from this image's 172 `.XTLID` pairs. The D3D device
  methods, the CRT names and the `CDirectSoundStream_Set*` names record no
  `xtlid` at all. The XTLID fallback in `sdk/tools/symbols/main.cpp:1040-1045`
  therefore yields zero proposals. (XTLID absence alone does not prove a body is
  absent — `.XTLID` only lists address-taken exports — so body presence below is
  judged by length / callee cluster / callers, in that order.)
- Caller analysis: for every unclaimed function in the D3D and DSOUND sections,
  IDA xrefs were counted by caller section (`.text` = game code, `XMV`, `XGRPH`,
  same-section). A `[symbols]` export the game uses must show game-section
  callers; internal helpers overwhelmingly show only same-section callers.

## 3. All 108 names

`sig len` is the length recorded in `sdk/xdk/5849/signatures.toml` (bytes).
Risk is stated only for real gaps: what breaks if the game calls the name while
it stays 0 (a 0 gets a trapping placeholder, `D:/Urbz/docs/gap-analysis.md:41-42`;
the host HLE body, where one exists, only binds once an address is supplied).

| name | class | absent section | risk if real-gap | sig sec | sig len |
| --- | --- | --- | --- | --- | --- |
| accept | expected-zero | XNET | — | XNET | 11 |
| bind | expected-zero | XNET | — | XNET | 11 |
| BinkDoFrame | expected-zero | BINK | — | BINK | 1497 |
| BinkGetSummary | expected-zero | BINK | — | BINK | 457 |
| BinkNextFrame | expected-zero | BINK | — | BINK | 413 |
| BinkWait | expected-zero | BINK | — | BINK | 499 |
| CDirectSoundStream_SetFilter | real-gap | — | audio only | DSOUND | 82 |
| CDirectSoundStream_SetFormat | real-gap | — | audio only | DSOUND | 82 |
| CDirectSoundStream_SetFrequency | real-gap | — | audio only | DSOUND | 82 |
| CDirectSoundStream_SetHeadroom | real-gap | — | audio only | DSOUND | 82 |
| CDirectSoundStream_SetI3DL2Source | real-gap | — | audio only | DSOUND | 86 |
| CDirectSoundStream_SetMaxDistance | real-gap | — | audio only | DSOUND | 90 |
| CDirectSoundStream_SetMinDistance | real-gap | — | audio only | DSOUND | 90 |
| CDirectSoundStream_SetPosition | real-gap | — | audio only | DSOUND | 107 |
| CDirectSoundStream_SetVelocity | real-gap | — | audio only | DSOUND | 107 |
| closesocket | expected-zero | XNET | — | XNET | 11 |
| connect | expected-zero | XNET | — | XNET | 11 |
| crt_atan | real-gap | — | cosmetic (numeric precision) | .text | 130 |
| crt_cos | real-gap | — | cosmetic (numeric precision) | .text | 145 |
| crt_log | real-gap | — | cosmetic (numeric precision) | .text | 184 |
| crt_log10 | real-gap | — | cosmetic (numeric precision) | .text | 184 |
| crt_sin | real-gap | — | cosmetic (numeric precision) | .text | 145 |
| crt_sqrt | real-gap | — | cosmetic (numeric precision) | .text | 157 |
| crt_tan | real-gap | — | cosmetic (numeric precision) | .text | 149 |
| D3DDevice_BeginPush | real-gap | — | renders nothing | — | — |
| D3DDevice_EnableOverlay | real-gap | — | renders nothing (video overlay) | D3D | 99 |
| D3DDevice_EndPush | real-gap | — | renders nothing | — | — |
| D3DDevice_FlushVertexCache | real-gap | — | cosmetic | D3D | 39 |
| D3DDevice_GetRenderTarget2 | real-gap | — | renders nothing | D3D | 26 |
| D3DDevice_GetVertexShaderFunction | real-gap | — | cosmetic | D3D | 188 |
| D3DDevice_LightEnable | real-gap | — | renders nothing | D3D | 337 |
| D3DDevice_PersistDisplay | real-gap | — | cosmetic | D3D | 302 |
| D3DDevice_SetIndices | real-gap | — | renders nothing | D3D | 132 |
| D3DDevice_SetLight | real-gap | — | renders nothing | D3D | 691 |
| D3DDevice_SetMaterial | real-gap | — | renders nothing | D3D | 48 |
| D3DDevice_SetPalette | real-gap | — | renders nothing (P8 textures) | — | — |
| D3DDevice_SetTile | real-gap | — | cosmetic | D3D | 10 |
| D3DDevice_SetVertexShaderConstant1Fast | real-gap | — | renders nothing | D3D | 73 |
| D3DDevice_SwitchTexture | real-gap | — | renders nothing | D3D | 48 |
| D3DResource_BlockUntilNotBusy | real-gap | — | renders nothing (sync) | D3D | 5 |
| DirectSoundCreateBuffer | real-gap | — | audio only | DSOUND | 87 |
| DirectSoundUseFullHRTF | real-gap | — | audio only | DSOUND | 31 |
| DirectSoundUseLightHRTF | real-gap | — | audio only | — | — |
| getsockname | expected-zero | XNET | — | XNET | 11 |
| getsockopt | expected-zero | XNET | — | XNET | 15 |
| IDirectSound_CommitDeferredSettings | real-gap | — | audio only | DSOUND | 24 |
| IDirectSound_CommitEffectData | real-gap | — | audio only | — | — |
| IDirectSound_EnableHeadphones | real-gap | — | audio only | DSOUND | 28 |
| IDirectSound_GetEffectData | real-gap | — | audio only | — | — |
| IDirectSound_GetOutputLevels | real-gap | — | audio only | — | — |
| IDirectSound_GetSpeakerConfig | real-gap | — | audio only | DSOUND | 28 |
| IDirectSound_SetDistanceFactor | real-gap | — | audio only | DSOUND | 36 |
| IDirectSound_SetDopplerFactor | real-gap | — | audio only | DSOUND | 36 |
| IDirectSound_SetEffectData | real-gap | — | audio only | DSOUND | 42 |
| IDirectSound_SetI3DL2Listener | real-gap | — | audio only | DSOUND | 32 |
| IDirectSound_SetMixBinHeadroom | real-gap | — | audio only | DSOUND | 32 |
| IDirectSound_SetOrientation | real-gap | — | audio only | DSOUND | 74 |
| IDirectSound_SetPosition | real-gap | — | audio only | DSOUND | 53 |
| IDirectSound_SetRolloffFactor | real-gap | — | audio only | DSOUND | 36 |
| IDirectSound_SetVelocity | real-gap | — | audio only | DSOUND | 53 |
| IDirectSoundBuffer_GetCurrentPosition | real-gap | — | audio only | DSOUND | 32 |
| IDirectSoundBuffer_Lock | real-gap | — | audio only | DSOUND | 48 |
| IDirectSoundBuffer_SetAllParameters | real-gap | — | audio only | — | — |
| IDirectSoundBuffer_SetCurrentPosition | real-gap | — | audio only | DSOUND | 28 |
| IDirectSoundBuffer_SetFilter | real-gap | — | audio only | DSOUND | 28 |
| IDirectSoundBuffer_SetHeadroom | real-gap | — | audio only | DSOUND | 28 |
| IDirectSoundBuffer_SetI3DL2Source | real-gap | — | audio only | DSOUND | 32 |
| IDirectSoundBuffer_SetMaxDistance | real-gap | — | audio only | DSOUND | 36 |
| IDirectSoundBuffer_SetMinDistance | real-gap | — | audio only | DSOUND | 36 |
| IDirectSoundBuffer_SetOutputBuffer | real-gap | — | audio only | — | — |
| IDirectSoundBuffer_SetPosition | real-gap | — | audio only | DSOUND | 53 |
| IDirectSoundBuffer_SetRolloffCurve | real-gap | — | audio only | — | — |
| IDirectSoundBuffer_SetVelocity | real-gap | — | audio only | DSOUND | 53 |
| IDirectSoundBuffer_StopEx | real-gap | — | audio only | — | — |
| IDirectSoundBuffer_Unlock | real-gap | — | audio only | DSOUND | 5 |
| IDirectSoundStream_SetEG | real-gap | — | audio only | DSOUND | 5 |
| ioctlsocket | expected-zero | XNET | — | XNET | 11 |
| listen | expected-zero | XNET | — | XNET | 11 |
| recv | expected-zero | XNET | — | XNET | 11 |
| recvfrom | expected-zero | XNET | — | XNET | 15 |
| select | expected-zero | XNET | — | XNET | 15 |
| send | expected-zero | XNET | — | XNET | 11 |
| sendto | expected-zero | XNET | — | XNET | 15 |
| setsockopt | expected-zero | XNET | — | XNET | 15 |
| shutdown | expected-zero | XNET | — | XNET | 11 |
| socket | expected-zero | XNET | — | XNET | 11 |
| WSACancelOverlappedIO | expected-zero | XNET | — | XNET | 11 |
| WSAGetLastError | expected-zero | XNET | — | XNET | 5 |
| WSAGetOverlappedResult | expected-zero | XNET | — | XNET | 15 |
| WSARecv | expected-zero | XNET | — | XNET | 15 |
| WSARecvFrom | expected-zero | XNET | — | XNET | 15 |
| WSASend | expected-zero | XNET | — | XNET | 15 |
| WSASendTo | expected-zero | XNET | — | XNET | 15 |
| WSAStartup | expected-zero | XNET | — | XNET | 27 |
| XAudioDownloadEffectsImage | real-gap | — | audio only | DSOUND | 514 |
| XNetCreateKey | expected-zero | XNET | — | XNET | 11 |
| XNetDnsLookup | expected-zero | XNET | — | XNET | 23 |
| XNetDnsRelease | expected-zero | XNET | — | XNET | 11 |
| XNetGetEthernetLinkStatus | real-gap | — | unused | .text | 72 |
| XNetGetTitleXnAddr | expected-zero | XNET | — | XNET | 11 |
| XNetInAddrToXnAddr | expected-zero | XNET | — | XNET | 23 |
| XNetRandom | expected-zero | XNET | — | XNET | 11 |
| XNetRegisterKey | expected-zero | XNET | — | XNET | 11 |
| XNetStartup | expected-zero | XNET | — | XNET | 23 |
| XNetUnregisterKey | expected-zero | XNET | — | XNET | 11 |
| XNetXnAddrToInAddr | expected-zero | XNET | — | XNET | 23 |
| XOnlineCleanup | expected-zero | XONLINE | — | XONLINE | 111 |
| XOnlineStartup | expected-zero | XONLINE | — | XONLINE | 253 |

Counts: 40 expected-zero (BINK 4, XNET 34, XONLINE 2), 68 real-gap
(D3D 16, DSOUND 44, CRT 7, `XNetGetEthernetLinkStatus` 1).

Notes on individual groups:

- **BINK (4).** `BinkDoFrame`, `BinkGetSummary`, `BinkNextFrame`, `BinkWait` are
  signed against section `BINK`; this XBE has no BINK section (the disc's movies
  are XMV, cf. `D:/Urbz/docs/gap-analysis.md:79-91` for the sibling). Expected zero.
- **XNET / Winsock (34).** `accept`, `bind`, `closesocket`, `connect`,
  `getsockname`, `getsockopt`, `ioctlsocket`, `listen`, `recv`, `recvfrom`,
  `select`, `send`, `sendto`, `setsockopt`, `shutdown`, `socket`, the eight
  `WSA*` names and the ten `XNet*` names are all signed against `XNET`, which
  this XBE does not contain. Expected zero. (Their `xtlid`s 0x40001… are likewise
  absent from `.XTLID`, consistent with the library never being linked.)
- **XONLINE (2).** `XOnlineStartup`, `XOnlineCleanup`, signed against `XONLINE`,
  absent. Expected zero.
- **`XNetGetEthernetLinkStatus` is the one exception to the network rule:** its
  signature is recorded against `.text` (len 72, `xtlid` 0x4003A), a section this
  XBE *has*, so it is classified real-gap. Its xtlid is absent from `.XTLID` and
  no 72-byte candidate with game callers was found; an Ethernet-link query in an
  offline title is unused in practice. Not worth chasing.
- **CRT (7).** `crt_sin/cos/tan/atan/sqrt/log/log10` are signed against `.text`
  (LIBCMT). Located neighbours prove the matcher works in this region
  (`crt_acos` 0x25A165, `crt_asin` 0x25A231, `crt_pow` 0x25A324, called by the
  `_CIacos`/`_CIasin` wrappers at 0x25A148/0x25A214), yet none of the seven
  matched — the same seven are unmatched in Urbz
  (`D:/Urbz/docs/gap-analysis.md:37,55-57`). The game code uses inline x87
  instead (e.g. `fsin`/`fcos` inside `.text` functions such as `sub_0001F0A0`,
  `sub_0001F1A0` in `build/sims2_flat.exe`), so the LIBCMT objects were most
  likely never pulled in. UNCONFIRMED: dead-stripped vs. differently-compiled
  bodies. Either way the game's own x87 runs, which the SDK only replaces for
  bit-exactness (Urbz doc lines 55-57). No addresses proposed.
- **DSOUND (44).** All signed against `DSOUND`, which the XBE has. Host bodies
  exist for the buffer/stream surface in `sdk/runtime/xdk/dsound.cpp`, but the
  DSP-effect, filter, I3DL2 and HRTF setters have no host reproduction
  (`D:/Urbz/docs/gap-analysis.md:61-66`), so even bound they are audio-only.
  The nine `CDirectSoundStream_Set*` names record no xtlid; see §6 for their
  candidate cluster. The tiny `IDirectSound*` / `IDirectSoundBuffer_*` forwarders
  (sig len 5-53, xtlid 0xB0037…) are not `.XTLID`-exported in this image and no
  game-section callers reach any unclaimed DSOUND function except the five-byte
  vtable thunks at 0x2A699D / 0x2A69A2 / 0x2A69A7 / 0x2A6F5B, which IDA shows are
  jumps to the already-located `CDirectSoundStream_SetVolume` /
  `_SetMixBinVolumes` / `_Pause` / `_SetMixBins` — not to any missing name.

## 4. Proposed [symbols] entries

Exactly two names meet the evidence bar. Paste-ready (same column style as
`config/game.toml` is *not* required — `name = 0xADDRESS` per line):

```
D3DDevice_SetPalette = 0x00272020
D3DDevice_SetVertexShaderConstant1Fast = 0x00272FD0
```

| name | proposed | evidence | confidence |
| --- | --- | --- | --- |
| `D3DDevice_SetPalette` | 0x00272020 | Pseudocode (`build/sims2_flat.exe:0x272020`): `__stdcall(stage, header)`; reads old header at `buf + 4*stage + 3992` (3992 = 0xF98 = `CDevice_Palettes`, `sdk/xdk/5849/layout.toml:14`, "D3DDevice_SetPalette … stores [stage]"), releases it via 0x270CF0, stores the new header at the same slot, refcounts it (`*a2 += 0x80000`), and pushes `(stage << 6) + 269088` (269088 = 0x41B20) with the header data. This is byte-for-byte the behaviour the Urbz config annotates for its own SetPalette at 0x24BB90 (`D:/Urbz/config/game.toml:102-109`). Neighbour order also fits: after located `D3DDevice_SetTexture` (0x271E70), before located `D3DDevice_SetScissors` (0x272200) — the same relative position as Urbz (SetTexture 0x24B9E0 → SetPalette 0x24BB90 → SetTile → SetScissors 0x24BD70). 7 callers from game `.text`. The signature file records `{}` for this name (never signed), so there is no recorded length to contradict; IDA length 141. | high |
| `D3DDevice_SetVertexShaderConstant1Fast` | 0x00272FD0 | Asm (`build/sims2_flat.exe:0x272FD0`): fastcall `ecx`=register, `edx`=source, stack dword count in `ebx`; `rep movsd` copies count dwords to `dword_280AB0 + 16*reg` (fast-constant scratch), then tail-calls located `D3DDevice_SetVertexShaderConstantNotInlineFast` (0x272EF0) with (reg, count), `retn 4`. The host implementation describes exactly this guest behaviour: "the guest original also copies the data into the fast-constant scratch … before tail-calling SetVertexShaderConstantNotInlineFast" (`sdk/runtime/xdk/d3d8.cpp:935-937`). Neighbour order matches Urbz exactly: NotInlineFast (Sims2 0x272EF0 / Urbz 0x24CE50) → this function → `D3DDevice_SetStreamSource` (Sims2 0x273010 / Urbz 0x24CF70) (`D:/Urbz/config/game.toml:149,162`). 2 callers from game `.text` (0x1FBE65, 0x1FBE73). Recorded sig length is 73 vs. 49 here — an inlining/build variance (the recorded callee cluster, a single 15-byte `MakeSpace`-class helper, does not survive either; the semantic fingerprint and neighbour order do). | high (semantic + neighbour order; length differs) |

No other proposal is made. In particular the six unclaimed 78-82-byte
`CDirectSoundStream_Set*`-shaped functions at 0x2A672E, 0x2A677C, 0x2A6E15,
0x2A6E63 (identical bodies to located `CDirectSoundStream_SetVolume` 0x2A67CA
except for the inner target/offset, e.g. `sub_002A5D25(a1, a2)` vs.
`sub_002A5D25(a1 + 4, a2)`) are **not** proposed: there are nine missing stream
setters for at most four such slots, and assigning the wrong name to a slot
would bind a host setter to the wrong guest field. They are listed in §6 with
what would be needed to separate them.

## 5. D3D names in scope explicitly shown absent from the image

For each remaining D3D name from the task list, the standalone body does not
exist in this image (or cannot be distinguished), with the check that was run:

| name | sig len / cluster | finding |
| --- | --- | --- |
| `D3DDevice_BeginPush` | `{}` — no signature has ever been recorded for it, in either title (Urbz also 0, `D:/Urbz/config/game.toml:68`) | No standalone body. Guest push-buffer access in this image goes through located `D3DDevice_MakeSpace` (0x275C50) plus direct push-pointer writes; the unclaimed game-called push writers at 0x273A80 / 0x273AC0 / 0x273B20 / 0x273B70 (26 / 25 / 19 / 2 `.text` callers) each write a method header (`8*a2 + 530560`, `0x100000 + …`, `4*a2 + 268608`), which BeginPush by definition does not (host contract: "BeginPush(dwords) hands out room", `sdk/runtime/xdk/d3d8.cpp:1253-1255`). No unclaimed D3D function with the BeginPush shape (return push pointer, no header write) has game callers. Treated as header-inlined / absent-as-function. UNCONFIRMED: inlined vs. dead-stripped. |
| `D3DDevice_EndPush` | `{}` — same as above (Urbz also 0) | No standalone body, same check as BeginPush. The located `D3DDevice_Begin` (0x273BC0) / `D3DDevice_End` (0x273C00) pair (11 game callers each) is the begin/end this title actually calls. Absent-as-function. |
| `D3DDevice_SetIndices` | 132; callee [145]; constants [0x7A0 ×4, 0x80000, 0xFFF80000] | No candidate. The recorded constants point at `CDevice_IndexBuffer` = +0x7A0 (`sdk/xdk/5849/layout.toml:8`: "D3DDevice_SetIndices … reads the old one, stores the new one", `CDevice_BaseVertex` +0x1C). Scanning the whole D3D section for the 0x7A0 displacement finds only surface-creation code (0x2786FA, in 0x278680) and a shadow-texture helper (0x27E4D0) — no 132-byte function storing to device+0x7A0, and no unclaimed D3D function of that size has game callers. The title indexes through located `D3DDevice_DrawIndexedVertices` (0x273820). Absent as a standalone export. |
| `D3DDevice_SetLight` | 691; callees [160, 26, 66, 206, 9, 9, 47] | No candidate. No D3D function of ~691 bytes is called by any ~337-byte function (full call-graph scan for callee length 600-760), and no unclaimed function with that callee cluster has game callers. Lighting state in this title is pushed by the game-called writers in §5 BeginPush row (notably 0x273A80, called in a per-index loop from game code at 0x1F5320). Absent as a standalone export. |
| `D3DDevice_LightEnable` | 337; callee [691 = SetLight] | No candidate, by the same scan: nothing calls a ~691-byte callee, so the SetLight→LightEnable pair does not exist as separate bodies here. Absent as a standalone export. |
| `D3DDevice_SetMaterial` | 48; constants [0xF00, 0x9000]; no callees | No candidate. The only 48-byte unclaimed D3D function with game callers, 0x2741F0, is a surface-descriptor initialiser (`a1[1] = a4 & 0xFFFFFFF; *a1 = 17104897; …`), not a material store; the 0xF00 displacement appears only in texture-stage code (0x27BF2D, 0x27C021). Absent as a standalone export. |
| `D3DDevice_SwitchTexture` | 48; callee [15 = MakeSpace-class]; fastcall (ecx, edx), ret 4 | No candidate. Every 48-byte-class unclaimed function was read (0x2741F0 above; 0x2720B0 is a 47-byte dirty-check `*(device+2356)…`); none takes (method, data, format) and rewrites a bound texture header (host contract `sdk/runtime/xdk/d3d8.cpp:816-830`). Absent as a standalone export. |
| `D3DDevice_FlushVertexCache` | 39; callee [15]; constant [0x41710] | No candidate. The push method 0x41710 appears nowhere in the D3D section (immediate scan), and no 39-byte unclaimed function calling located `D3DDevice_MakeSpace` (0x275C50) has game callers. The host body is a no-op (`sdk/runtime/xdk/d3d8.cpp:542`), so even if inlined somewhere this is cosmetic. Absent as a standalone export. |
| `D3DDevice_GetRenderTarget2` | 26; callee [55]; constant [0x1A04] | Absent as a standalone export. The recorded callee (55 bytes) is the internal AddRef helper at 0x270E10 (55 bytes), and the constant is `CDevice_RenderTarget` = +0x1A04 (`layout.toml:15`). In this image the only functions calling 0x270E10 and returning a device surface are located `D3DDevice_GetBackBuffer2` (0x2718A0, reads +6676) and `D3DDevice_GetDepthStencilSurface2` (0x271C20, reads +6664); +0x1A04 (6660) is read only inside located `D3DDevice_SetRenderTarget` (0x271620) and the mode-set/teardown family. No 26-byte getter exists. |
| `D3DDevice_GetVertexShaderFunction` | 188; ret 12; constants include 0x88760867 (D3DERR_MOREDATA) | No candidate. No unclaimed D3D function of ~188 bytes with game callers references 0x88760867; shader readback is not exercised by this title's code paths found. Absent as a standalone export. UNCONFIRMED: could exist uncalled in the 0x27B000+ driver tail, which contains no game-called functions matching the constant set. |
| `D3DDevice_PersistDisplay` | 302; callees [17, 120, 803, 17, 101, 98, 71] | No candidate. No unclaimed ~302-byte function with that callee cluster has game or XMV callers (the 303-byte 0x277AE0 is the surface teardown of §1, with an unrelated body). Absent as a standalone export; cosmetic regardless (quick-reboot front-buffer save, host `sdk/runtime/xdk/d3d8.cpp:532-541`). |
| `D3DDevice_EnableOverlay` | 99; constants [0x934, 0x1000, 0x8100, 0x8700…, 0x89xx] | No candidate. Callers would have to come from the XMV section; the only XMV-called unclaimed D3D functions are 0x2714A0 (193 bytes), 0x2720E0 (60 bytes, a display-mode query: reads device+1911/+1914/+1918) and the lock forwarders 0x2742B0 / 0x2742D0 — none is a 99-byte overlay enable, and none references the 0x81xx/0x87xx/0x89xx overlay register constants. Absent as a standalone export. This is the known XMV-presentation gap (`D:/Urbz/docs/gap-analysis.md:85-91`); the host body is `return 0` (`sdk/runtime/xdk/d3d8.cpp:529`). |
| `D3DDevice_SetTile` | 10; callees [17, 209] | No standalone body. A 10-byte export cannot contain two rel32 calls; the recorded cluster is the *jump targets* it forwards to. The 209-byte callee matches internal 0x272120 (§1) exactly in length, but xrefs to 0x272120 come only from 0x277C10 and 0x277AE0 — there is no 10-byte forwarder to it anywhere in the D3D section. The tile work is done by calling 0x272120 directly. Absent as a standalone export; host body is `return 0` (`sdk/runtime/xdk/d3d8.cpp:1307`). |
| `D3DResource_BlockUntilNotBusy` | 5; xtlid 0x8006A; callee [120] | Absent from the image. Its xtlid 0x8006A is not in `.XTLID` (neighbours 0x80069 → 0x270E10 and 0x8006E → 0x270EA0 are, and are located). The recorded 120-byte callee is located `D3D_BlockOnResource` (0x275CD0, 120 bytes), but the only 5-byte jumps in the D3D section are 0x270C10 → 0x278C70 and 0x271270 → `D3D_KickOffAndWaitForIdle` (0x275CB0); neither targets 0x275CD0. Callers that need the behaviour call `D3D_BlockOnResource` directly (e.g. 0x270CF0, 0x278F60). |

`D3DDevice_SetPalette` and `D3DDevice_SetVertexShaderConstant1Fast` are the
exceptions: both are present and proposed in §4. Together with the fourteen
rows above, every D3D name in the task list is accounted for.

## 6. Names with no guest callers — not worth chasing

Criterion: no xref from game code (`.text`, `XMV`, `XGRPH`) to any candidate
body, and (where recorded) xtlid absent from `.XTLID`. If the game never calls
them, the trapping placeholder at 0 never fires
(`D:/Urbz/docs/gap-analysis.md:41-42`).

- All 40 expected-zero names (§3): the libraries were never linked.
- `crt_atan`, `crt_cos`, `crt_log`, `crt_log10`, `crt_sin`, `crt_sqrt`,
  `crt_tan` — inline x87 in game code instead (§3); only bit-exactness is at
  stake.
- `XNetGetEthernetLinkStatus` — offline title, no candidate, no callers.
- D3D, per §5: `D3DDevice_BeginPush`, `D3DDevice_EndPush`,
  `D3DDevice_SetIndices`, `D3DDevice_SetLight`, `D3DDevice_LightEnable`,
  `D3DDevice_SetMaterial`, `D3DDevice_SwitchTexture`,
  `D3DDevice_FlushVertexCache`, `D3DDevice_GetRenderTarget2`,
  `D3DDevice_GetVertexShaderFunction`, `D3DDevice_PersistDisplay`,
  `D3DDevice_EnableOverlay`, `D3DDevice_SetTile`,
  `D3DResource_BlockUntilNotBusy`.
- DSOUND tiny forwarders with no identified caller and no `.XTLID` export:
  `DirectSoundCreateBuffer`, `DirectSoundUseFullHRTF`,
  `DirectSoundUseLightHRTF`, `XAudioDownloadEffectsImage`, all fifteen
  `IDirectSound_*` names, all fifteen `IDirectSoundBuffer_*` names and
  `IDirectSoundStream_SetEG`. (The game drives sound through the located
  `DirectSoundCreate` / `DirectSoundCreateStream` / buffer `Play`/`Stop`/
  `SetVolume`/`SetFrequency` surface, all located; the 3D/effect setters are
  exactly the surface the host does not reproduce anyway,
  `D:/Urbz/docs/gap-analysis.md:61-66`.)
- The nine `CDirectSoundStream_Set*` setters (`SetFilter`, `SetFormat`,
  `SetFrequency`, `SetHeadroom`, `SetI3DL2Source`, `SetMaxDistance`,
  `SetMinDistance`, `SetPosition`, `SetVelocity`) are the *only* DSOUND names
  worth a second look, and only if audio triage later shows stream setters
  being exercised: four same-shaped candidate slots exist (0x2A672E, 0x2A677C
  before located `CDirectSoundStream_SetVolume` 0x2A67CA; 0x2A6E15, 0x2A6E63
  before located `CDirectSoundStream_SetMixBins` 0x2A6EB1; IDA lengths
  78-82 vs. recorded 82-107). Separating them requires tracing each slot's
  inner call (e.g. `sub_002A5D25` at differing `this` offsets) to the field it
  writes and matching that field to the XDK stream layout — not done here, and
  no name is proposed for any slot.

## 7. Summary

- Crash (§1): guest internal D3D display-driver code (0x271570 → 0x277C10 →
  0x272120 → 0x27AD76 → 0x27AA61) spins on NV2A status at
  `*(CDevice+0x1C28) + 0x3214` with the miniport pointer 0. No `[symbols]` name
  exists for any function on the path, in either title; no listed D3D name is
  on the path. This needs a host-side fix, not a config line.
- 108 names classified: 40 expected-zero, 68 real-gap (§3).
- 2 addresses proposed with evidence (§4): `D3DDevice_SetPalette` 0x00272020,
  `D3DDevice_SetVertexShaderConstant1Fast` 0x00272FD0.
- All 16 D3D names accounted for: 2 proposed, 14 shown absent as standalone
  exports with the check that was run (§5).
- Everything else is either never linked or never called (§6).
