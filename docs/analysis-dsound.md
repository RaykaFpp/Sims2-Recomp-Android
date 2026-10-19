# DirectSound gap plan — The Sims 2 (USA), XDK 5849

Scope: the 44 `DSOUND` names classified `real-gap` in
`docs/analysis-xdk-gaps.md` §3. Question: what does the title need from
DirectSound that the host does not yet provide, and what are the fixes?
Answer up front: **the title calls none of the 44, every one of the 44 already
has a host replacement in `sdk/runtime/xdk/dsound.cpp`, and no
`[symbols]` line can or should be added for any of them.** The only
DirectSound work left is to keep it that way (regression-watch the
`AuditExports` log line, §6). No address is proposed in this document;
proposing one would require inventing it, which §3 shows is impossible for
these names in this image.

Evidence base: `build/sims2_flat.exe` in IDA (guest VAs map 1:1),
`sdk/xdk/5849/signatures.toml`, `config/game.toml`,
`D:/Urbz/config/game.toml`, `D:/Urbz/docs/gap-analysis.md`. Section ranges
from `docs/analysis-xdk-gaps.md:6-7`: `.text` 0x11000..0x270880,
`DSOUND` 0x2A5740..0x2AF920.

## 1. Method — how "the game calls it" was established

Three independent checks, all must agree before a name is called reachable:

1. **Direct-call census.** Every `call`/`jmp` in `.text` whose target falls
   in `DSOUND` was enumerated in IDA. Result: **26 distinct targets**, all
   located names or 5-byte thunks to located names (full list in §2.1).
   Buffer/device methods in this library are called exactly this way —
   e.g. located `IDirectSoundBuffer_SetVolume` (0x2A68AD) has 4 `.text`
   callers (`0x212C65`, `0x212F3D`, `0x212FE5`, `0x2130E7`), located
   `IDirectSoundBuffer_SetFrequency` (0x2A6F03) has 5 — so a used
   buffer/device export necessarily appears in this census.
2. **`.XTLID` census.** All 173 id/address pairs at guest 0x401100 were
   decoded. The `DSOUND` library id is `0xB`. Every missing name that
   records an `xtlid` (0xB0003…0xB00A6 range, see table) is **absent**;
   present neighbours prove the table was read correctly, e.g.
   `0xB0002 → DirectSoundCreate` (0x2A7CB8), `0xB0004 →
   DirectSoundCreateStream` (0x2A7CFF), `0xB003C →
   IDirectSound_DownloadEffectsImage` (0x2A686E), `0xB0057 →
   IDirectSoundBuffer_Pause` (0x2A6945), `0xB009E → 0x2A699D` (stream
   `SetVolume` thunk). The `CDirectSoundStream_Set*` names record no
   `xtlid` at all in `signatures.toml`; they are reached, when present,
   through the same 5-byte thunk pattern as located `SetVolume`
   (0x2A699D, 3 `.text` callers) — and no thunk exists for any missing
   stream setter (the only stream thunks are 0x2A699D → `SetVolume`,
   0x2A69A2 → `SetMixBinVolumes`, 0x2A69A7 → `Pause`, 0x2A6F5B →
   `SetMixBins`, 0x2A69AC → `FlushEx` wrapper).
3. **Body census.** Every IDA function in `DSOUND` was listed with length
   and xref counts split by caller section. A present export must show a
   body whose length matches `signatures.toml` (allowing the 78-vs-82
   wrapper/inner split explained in §5) and whose nested callee lengths
   match the recorded cluster. For all 44 names, no such unclaimed body
   exists in the voice/device core region (0x2A5740..0x2A7FF9); the only
   setter-shaped bodies there are accounted for in §5. Bodies elsewhere
   in `DSOUND` (0x2A8000+, the WMA/XMV-adjacent decoder tail) were also
   scanned by length: none carries a missing name's callee cluster and
   none has a `.text` caller.

Stream methods need one caveat, checked explicitly: located stream
methods (`AddRef` 0x2A610E … `Flush` 0x2A6257) have **zero** direct
`.text` callers and are reached via the stream vtables at guest
0x2F1B38 / 0x2F1B28 (data xrefs, e.g. `0x2F1B38+0x00 → AddRef`,
`+0x18 → Flush`). The vtables' remaining slots point at generic
refcount/destructor stubs (`0x2A57EC` inc-ref, `0x2A580A` dec-ref,
`0x2A57F9` release-tail, destructors `0x2A7AD3`, `0x2A78A2`) — **no
vtable slot points at any missing stream setter**, and the game-facing
setters that do exist (`SetVolume`, `SetMixBinVolumes`, `SetMixBins`)
are called through the thunks in check 2, not the vtable. So the
zero direct-call count for stream setters is not a vtable blind spot.

Caller counts below are therefore reported as `.text` direct calls /
`DSOUND` direct calls **to the name's own body**. `0 / 0 — no body`
means checks 1–3 all came back empty: there is no address in the image
that a call could target. This matches `docs/analysis-xdk-gaps.md:396-404`
and extends it from "no game-section callers reach any unclaimed
function" to a per-name, per-interface proof.

### 2.1 What the game actually calls (for contrast)

Complete `.text → DSOUND` target list (IDA census; counts are `.text`
call sites): `DirectSoundCreate` 0x2A7CB8 ×1 (`0x1E862B`),
`IDirectSound_Release` 0x2A5A0A ×1, `IDirectSound_DownloadEffectsImage`
0x2A686E ×1 (`0x1E90D1`), `DirectSoundCreateStream` 0x2A7CFF ×1
(`0x212330`, in game stream setup `_sub_00212270`),
`IDirectSound_CreateSoundBuffer` 0x2A7AEE ×1,
`DirectSoundDoWork` 0x2A69C4 ×1, buffer wrappers
`SetVolume` ×4, `PlayEx` ×3, `GetStatus` ×3, `Pause` ×2,
`SetLoopRegion` ×2, `SetPlayRegion` ×2, `SetFrequency` ×5,
`SetBufferData` ×1, `SetFormat` ×1, `SetMixBins` ×1,
`SetMixBinVolumes` ×1, `Stop` ×1, `Release` ×1, stream thunks
0x2A699D (`SetVolume`) ×3, 0x2A69A2 (`SetMixBinVolumes`) ×1,
0x2A69A7 (`Pause`) ×4, 0x2A6F5B (`SetMixBins`) ×1, plus format helpers
0x2A5A80 ×1 / 0x2A5A85 ×2 (WAVEFORMATEX builders, XTLID 0xB00A5/0xB00A4)
and stream-create helper 0x2A69ED ×1. Every target is located in
`config/game.toml` and host-implemented. The game's audio engine
(`_sub_00212270` stream setup, `_sub_00212B30` buffer create,
`_sub_00212DE0` voice start/stop, `_sub_002133A0` mix-bin setup) uses
exactly this surface and no other.

## 2. The 44 names: caller counts, host coverage, risk

Risk columns: **Actual** = risk in the current build. Because every
count is zero, the trapping placeholder at `[symbols] = 0`
(`D:/Urbz/docs/gap-analysis.md:41-42`) can never fire, so Actual is
`None` for all 44 — stated once here instead of repeated per row.
**If-run** = what would happen *if* the XDK-library body ran on a host
object, judged from the disassembly of the nearest present relative
(located sibling wrapper + the inner voice/device function it calls,
§4). Ranks: `C` crash (unchecked inner/device dereference),
`C→H` crash first, hardware-poll hang behind it, `S` silent
(feature quietly absent/wrong, no dereference). `If-run` never changes
the fix (there is none); it orders the damage **if a future title
update, a wrong `[symbols]` guess, or an XTLID-driven guest-code path
ever reached the body** — i.e. it is the cost of guessing wrong in §5.

Host column gives the existing replacement in
`sdk/runtime/xdk/dsound.cpp` (`HLE_` = `HLE_STDCALL`,
`BUF`/`STR` = `BUFFER`/`STREAM` macro instantiations, which generate
`HLE_STDCALL(IDirectSoundBuffer_…)` / `HLE_STDCALL(CDirectSoundStream_…)`).
Line numbers are the definition lines in that file.

| # | Name | sig len / xtlid | .text / DSOUND callers | Host replacement | If-run |
| --- | --- | --- | --- | --- | --- |
| 1 | `CDirectSoundStream_SetFilter` | 82 / — | 0 / 0 — no body, no thunk | `STR(SetFilter,2)` dsound.cpp:888 (`return 0`) | C |
| 2 | `CDirectSoundStream_SetFormat` | 82 / — | 0 / 0 — no body, no thunk | `STR(SetFormat,2)` :823 (flush + `ReadFormat`) | C |
| 3 | `CDirectSoundStream_SetFrequency` | 82 / — | 0 / 0 — no body, no thunk | `STR(SetFrequency,2)` :884 | C→H |
| 4 | `CDirectSoundStream_SetHeadroom` | 82 / — | 0 / 0 — no body, no thunk | `STR(SetHeadroom,2)` :891 | C |
| 5 | `CDirectSoundStream_SetI3DL2Source` | 86 / — | 0 / 0 — no body, no thunk | `STR(SetI3DL2Source,3)` :916 (`return 0`) | C |
| 6 | `CDirectSoundStream_SetMaxDistance` | 90 / — | 0 / 0 — no body, no thunk | `STR(SetMaxDistance,3)` :903 | C |
| 7 | `CDirectSoundStream_SetMinDistance` | 90 / — | 0 / 0 — no body, no thunk | `STR(SetMinDistance,3)` :907 | C |
| 8 | `CDirectSoundStream_SetPosition` | 107 / — | 0 / 0 — no body, no thunk | `STR(SetPosition,5)` :911 | C |
| 9 | `CDirectSoundStream_SetVelocity` | 107 / — | 0 / 0 — no body, no thunk | `STR(SetVelocity,5)` :915 (`return 0`) | C |
| 10 | `DirectSoundCreateBuffer` | 87 / 0xB0003 absent | 0 / 0 — no body | `HLE_(DirectSoundCreateBuffer,2)` :593 (`NewBuffer`) | C |
| 11 | `DirectSoundUseFullHRTF` | 31 / 0xB002F absent | 0 / 0 — no body | `HLE_(…,0)` :487 (`return 0`) | S |
| 12 | `DirectSoundUseLightHRTF` | — / 0xB0031 absent (xtlid-only) | 0 / 0 — no body | `HLE_(…,0)` :488 (`return 0`) | S |
| 13 | `IDirectSound_CommitDeferredSettings` | 24 / 0xB0037 absent | 0 / 0 — no body | `HLE_(…,1)` :496 (`return 0`) | C |
| 14 | `IDirectSound_CommitEffectData` | — / 0xB0038 absent (xtlid-only) | 0 / 0 — no body | `HLE_(…,1)` :584 (`return 0`) | S |
| 15 | `IDirectSound_EnableHeadphones` | 28 / 0xB003D absent | 0 / 0 — no body | `HLE_(…,2)` :494 (`return 0`) | S* |
| 16 | `IDirectSound_GetEffectData` | — / 0xB003F absent (xtlid-only) | 0 / 0 — no body | `HLE_(…,5)` :580 (zero-fills caller buffer) | S |
| 17 | `IDirectSound_GetOutputLevels` | — / 0xB0040 absent (xtlid-only) | 0 / 0 — no body | `HLE_(…,3)` :586 (zero-fills 64 bytes) | S |
| 18 | `IDirectSound_GetSpeakerConfig` | 28 / 0xB0041 absent | 0 / 0 — no body | `HLE_(…,2)` :489 (writes `DSSPEAKER_STEREO`) | S |
| 19 | `IDirectSound_SetDistanceFactor` | 36 / 0xB0048 absent | 0 / 0 — no body | `HLE_(…,3)` :499 (`return 0`) | C |
| 20 | `IDirectSound_SetDopplerFactor` | 36 / 0xB0049 absent | 0 / 0 — no body | `HLE_(…,3)` :497 (`return 0`) | C |
| 21 | `IDirectSound_SetEffectData` | 42 / 0xB004A absent | 0 / 0 — no body | `HLE_(…,6)` :493 (`return 0`) | S |
| 22 | `IDirectSound_SetI3DL2Listener` | 32 / 0xB004B absent | 0 / 0 — no body | `HLE_(…,3)` :501 (`return 0`) | C |
| 23 | `IDirectSound_SetMixBinHeadroom` | 32 / 0xB004C absent | 0 / 0 — no body | `HLE_(…,3)` :495 (`return 0`) | C |
| 24 | `IDirectSound_SetOrientation` | 74 / 0xB004D absent | 0 / 0 — no body | `HLE_(…,8)` :507 (stores front/top) | C |
| 25 | `IDirectSound_SetPosition` | 53 / 0xB004E absent | 0 / 0 — no body | `HLE_(…,5)` :502 (stores listener) | C |
| 26 | `IDirectSound_SetRolloffFactor` | 36 / 0xB004F absent | 0 / 0 — no body | `HLE_(…,3)` :498 (`return 0`) | C |
| 27 | `IDirectSound_SetVelocity` | 53 / 0xB0050 absent | 0 / 0 — no body | `HLE_(…,5)` :500 (`return 0`) | C |
| 28 | `IDirectSoundBuffer_GetCurrentPosition` | 32 / 0xB0053 absent | 0 / 0 — no body | `BUF(GetCurrentPosition,3)` :684 | C |
| 29 | `IDirectSoundBuffer_Lock` | 48 / 0xB0056 absent | 0 / 0 — no body | `BUF(Lock,8)` :694 | C |
| 30 | `IDirectSoundBuffer_SetAllParameters` | — / 0xB005F absent (xtlid-only) | 0 / 0 — no body | `BUF(SetAllParameters,3)` :742 | C |
| 31 | `IDirectSoundBuffer_SetCurrentPosition` | 28 / 0xB0064 absent | 0 / 0 — no body | `BUF(SetCurrentPosition,2)` :690 | C |
| 32 | `IDirectSoundBuffer_SetFilter` | 28 / 0xB0068 absent | 0 / 0 — no body | `BUF(SetFilter,2)` :714 (`return 0`) | C |
| 33 | `IDirectSoundBuffer_SetHeadroom` | 28 / 0xB006B absent | 0 / 0 — no body | `BUF(SetHeadroom,2)` :715 | C→H |
| 34 | `IDirectSoundBuffer_SetI3DL2Source` | 32 / 0xB006C absent | 0 / 0 — no body | `BUF(SetI3DL2Source,3)` :740 (`return 0`) | C |
| 35 | `IDirectSoundBuffer_SetMaxDistance` | 36 / 0xB006F absent | 0 / 0 — no body | `BUF(SetMaxDistance,3)` :727 | C |
| 36 | `IDirectSoundBuffer_SetMinDistance` | 36 / 0xB0070 absent | 0 / 0 — no body | `BUF(SetMinDistance,3)` :731 | C |
| 37 | `IDirectSoundBuffer_SetOutputBuffer` | — / 0xB0075 absent (xtlid-only) | 0 / 0 — no body | `BUF(SetOutputBuffer,2)` :755 (`return 0`) | C* |
| 38 | `IDirectSoundBuffer_SetPosition` | 53 / 0xB0078 absent | 0 / 0 — no body | `BUF(SetPosition,5)` :735 | C |
| 39 | `IDirectSoundBuffer_SetRolloffCurve` | — / 0xB0079 absent (xtlid-only) | 0 / 0 — no body | `BUF(SetRolloffCurve,4)` :750 | C |
| 40 | `IDirectSoundBuffer_SetVelocity` | 53 / 0xB007B absent | 0 / 0 — no body | `BUF(SetVelocity,5)` :739 (`return 0`) | C |
| 41 | `IDirectSoundBuffer_StopEx` | — / 0xB007E absent (xtlid-only) | 0 / 0 — no body | `BUF(StopEx,4)` :651 | C |
| 42 | `IDirectSoundBuffer_Unlock` | 5 / 0xB007F absent | 0 / 0 — no body (no 5-byte jmp to a lock-core exists unclaimed) | `BUF(Unlock,5)` :613 (`return 0`) | C* |
| 43 | `IDirectSoundStream_SetEG` | 5 / 0xB008C absent | 0 / 0 — no body, no thunk | `HLE_(IDirectSoundStream_SetEG,2)` :890 (`return 0`) | C |
| 44 | `XAudioDownloadEffectsImage` | 514 / 0xB00A6 absent | 0 / 0 — no body (game uses located `IDirectSound_DownloadEffectsImage` ×1 instead) | `HLE_(…,4)` :558 (builds zeroed effect descriptor) | S |

`S*` / `C*` = judgement rests on the recorded callee cluster only (no
present sibling body close enough to read); marked UNCONFIRMED in §4.
All other `If-run` judgements cite a read body in §4.

**No `[symbols]` proposal is made for any row.** The activation line
the task asks for (`Name = 0xADDRESS`) presupposes a located guest
address; checks 1–3 show none exists, and `D:/Urbz/config/game.toml`
independently leaves all 44 at `0` against the same XDK build (Urbz
lines 35–45, 170–174, 177–224, 245), confirming this is a property of
the library/title pairing, not a Sims 2 matching failure. The host
replacements above bind by name in the runtime regardless; they simply
have no guest entry point to replace until and unless a caller appears,
which is the correct end state.

## 3. Why no address can be supplied (evidence standard)

A proposal needs at least two of: byte length vs
`sdk/xdk/5849/signatures.toml`, nested callee-length cluster, `.XTLID`
id, neighbour order vs already-named symbols. For the 44:

- **`.XTLID`**: every recorded id is absent (§1, check 2). Zero of 44
  pass.
- **Length + cluster**: the DSOUND core contains exactly eight
  setter-shaped bodies of the relevant family (five 78-byte inners,
  three located 82-byte stream wrappers, §5) and eight located 24–36
  byte buffer/device adjustor wrappers (`SetVolume`, `SetMixBinVolumes`,
  `Play`, `PlayEx`, `Stop`, `Pause`, `SetLoopRegion`, `GetStatus`,
  `SetFrequency`, `SetMixBins`, `SetPlayRegion`, `SetFormat`,
  `SetBufferData`, `CreateSoundBuffer`, `DownloadEffectsImage`,
  `SynchPlayback` — all located, all with `.text` or XTLID presence).
  No ninth body matches any missing name's length/cluster pair. In
  particular the recorded clusters discriminate: e.g.
  `SetMaxDistance` vs `SetMinDistance` differ only in nested callee hash
  (`0x8876…` vs `0xB7A8…`, the min/max clamp helpers), and neither
  0x33-byte nested callee exists as an unclaimed function called from
  an unclaimed 90-byte wrapper — both wrappers are simply not linked.
- **Neighbour order**: located stream wrappers sit at 0x2A67CA
  (`SetVolume`), 0x2A681C (`SetMixBinVolumes`), 0x2A6EB1 (`SetMixBins`)
  with the buffer inners interleaved (§5); there is no unclaimed slot
  in that run into which a missing stream setter could be inserted
  without displacing an identified body.

Conclusion: the library dead-stripped these exports because the title
never referenced them (same mechanism as the CRT x87 case,
`docs/analysis-xdk-gaps.md:304-314`). They are real gaps in the SDK
symbol universe and non-gaps in this binary.

## 4. Risk ranking — what the bodies actually do

Host objects are small zeroed blobs with no guest inner pointers:
buffers/streams are allocated by `NewBuffer` / `DirectSoundCreateStream`
(`dsound.cpp:396-414`, `:765-788`; `Voice` state lives host-side in
`g_voices`, the guest object holds only a vtable pair for streams).
Guest library code, by contrast, assumes the console layout. The
present relatives all share one shape:

- **Adjustor wrapper** (24–39 bytes, located examples read in full):
  branchless null-adjusted `this` (`add eax, -0x1C` for buffers,
  `-0x8` for the device; `neg/sbb/and` passes 0 through unchanged),
  then tail-call the inner. E.g. `IDirectSoundBuffer_SetVolume`
  0x2A68AD, `IDirectSound_DownloadEffectsImage` 0x2A686E.
- **Inner** (77–133 bytes): enters the library critical section via
  `_sub_002A597E` (0x2A597E: `fs:24h` test + `dword_2DEE1C` lock),
  returns `0x80004005` (E_FAIL) if `dword_2AF0A8` says uninitialised,
  otherwise dereferences the inner voice/device pointer **without a
  null check**: buffer inners read `[this+0x20]` (voice core, e.g.
  Play inner 0x2A5EB0 at 0x2A5EDC, Stop inner 0x2A5F5A at 0x2A5F86,
  GetStatus inner 0x2A5FFA at 0x2A6026), setter inners read
  `[this+0x10]` then write a field at a small offset (volume inner
  `_sub_002A5D25` 0x2A5D25: `mov eax,[ecx+10h]; sub edx,[eax+20h];
  mov [eax+1Ch],edx`), device inners read `[this+0xC]`/`[this+0x8]`
  (DownloadEffectsImage inner `_sub_002A5B4A` 0x2A5B4A at
  0x2A5B7C/0x2A5B87, SynchPlayback inner `_sub_002A5BAE` at 0x2A5BDA).

On a host object those inner pointers are 0, so the first field access
reads/writes at **0x18–0x20 off NULL** — the documented failure mode.
The host code says the same thing in one place, for the one located
name where this was wired deliberately: `BUFFER(Pause)` comment,
`dsound.cpp:655-658` ("on a host buffer object that inner pointer is
null and the guest code faults reading it"). Ranked:

### Rank C — crash on first touch (35 names: 31 `C`, 2 `C→H`, 2 `C*`)

All buffer/stream voice setters, getters and transport-adjacent names
(rows 1–10, 13, 19, 20, 22–42 minus the `S` rows, 43): their inners
follow the `[this+0x10]`/`[this+0x20]` pattern above. Sub-evidence by
family:

- *Volume/frequency/format/mix-bin setters*: inner bodies are present
  and read (§5) — `_sub_002A5D25` (volume field +0x1C),
  `_sub_002A5D0C` via `_sub_002A6CE2` (frequency field +0x18),
  `_sub_002A70FC` (format; dereferences `[esi+0xC]` and calls through
  `[eax+0x14]` at 0x2A7121), `_sub_002A6D06`/`_sub_002A5D41` (mix bins,
  writing `[ecx+bin*4+0x30]` via `_sub_002A5C2C`). Missing siblings
  (`SetFilter`, `SetHeadroom`, `SetMax/MinDistance`, `SetPosition`,
  `SetVelocity`, `SetI3DL2Source`, `SetAllParameters`,
  `SetRolloffCurve`, stream equivalents) record the same two-call
  cluster shape (`[0x1E critical-section, 0xNN field-writer]`) in
  `signatures.toml`, so they dereference identically. UNCONFIRMED only
  in the field offset, not in the dereference.
- *Position/transport getters*: `GetCurrentPosition` (cluster
  `[0x55 voice-core]`), `Lock` (`[0xE2 voice-core]`),
  `SetCurrentPosition`, `StopEx` — same `[this+0x20]` voice-core entry
  as the read Play/Stop/GetStatus inners. Crash.
- *Device 3D/listener setters* (rows 19, 20, 22–27, 13): recorded
  clusters all route through the 0x9A-byte (154) device-core callee
  (`SetDistanceFactor`, `SetDopplerFactor`, `SetRolloffFactor`,
  `SetI3DL2Listener`, `SetOrientation`, `SetPosition`, `SetVelocity`,
  `CommitDeferredSettings`), the same core the read device inners
  reach after `[this+0xC]`. Host device (`g_ds`, 0x40 zeroed,
  `dsound.cpp:480-481`) has no +0xC core: crash at a small offset.
- *Creation* (row 10): `DirectSoundCreateBuffer` cluster routes
  through the 0x6D-byte create callee into the buffer-construction
  path whose present relative `_sub_002A7B12` (313 bytes, called from
  `_sub_002A7C4B`) allocates and initialises a 0xB4/0x540-byte console
  voice against the device struct. Against the host device blob it
  reads device fields that are zero: crash or E_OUTOFMEMORY-class
  failure; ranked C (UNCONFIRMED which of the two fires first — the
  alloc-failure path returns `0x8007000E` cleanly if it gets that far).

### Rank C→H — crash first, poll-hang behind it (rows 3, 33; volume family generally)

The setter inners finish by flushing the voice to the APU:
`_sub_002AB6F3` (from `_sub_002A5D25`) and `_sub_002AB78B` (from
`_sub_002A5D0C`, `_sub_002A70FC`) write `ds:0FE8202F8`/`0FE820360`…
and spin on the hardware status word:

```
0x2AB722: mov edx, ds:0FE820010h
0x2AB728: shr edx, 2
0x2AB72B: cmp edx, eax
0x2AB72D: jb  short loc_2AB722     ; FIFO-not-drained poll
```

(identical poll at 0x2AB7DC in `_sub_002AB78B`). On the host,
0xFE820010 is not a mapped APU register window: if execution ever got
past the null-inner crash (it cannot on current host objects — the
field write at `[NULL+0x1C]` precedes the call), a status word reading
as 0 with a non-zero drain count loops forever. So these names are
hangs *in potentia* and crashes in practice; both orderings argue
against ever letting the guest body near a host object. This is the
audio analogue of the D3D miniport spin in
`docs/analysis-xdk-gaps.md:30-44`.

### Rank S — silent if run (rows 11, 12, 14, 16, 17, 18, 21, 44; 15 UNCONFIRMED)

- HRTF selectors (11, 12): free functions, no `this`; recorded inner
  (0x6F bytes for Full) is a global mode store. Host stubs return 0
  (`dsound.cpp:487-488`); guest body would likewise just flip HRTF
  mode the host mixer does not implement (`D:/Urbz/docs/gap-analysis.md:61-66`).
  Silent: positional audio stays stereo.
- Effect-data surface (21 `SetEffectData`, 14 `CommitEffectData`,
  16 `GetEffectData`, 44 `XAudioDownloadEffectsImage`): DSP effects
  are not reproduced by design (`dsound.cpp:533`); host answers with
  zeroed effect memory (`:580-583`, `:535-556`). The guest bodies
  would stage data for a DSP that does not exist here — the failure
  is absent reverb/effects, not a fault, **provided** the staging
  writes go through the parsed image descriptor. UNCONFIRMED for row
  44's 514-byte body beyond its cluster (it parses an image and calls
  the same descriptor builder family as located
  `IDirectSound_DownloadEffectsImage`, whose host twin is safe).
- `GetOutputLevels` (17) / `GetSpeakerConfig` (18): pure out-parameter
  writers on the host (`:586-589`, `:489-492`: 64 zero bytes /
  `DSSPEAKER_STEREO`). Guest bodies read global config; worst case is
  a wrong-but-plausible value (e.g. reporting AC-3 speakers the mixer
  does not drive). Silent.
- Row 15 `EnableHeadphones` is marked `S*` UNCONFIRMED: its 0x11B-byte
  (283) recorded callee is large enough to reach the device core, in
  which case it belongs in Rank C. It is the only `S` row whose
  cluster leaves the question open; it changes no action either way.

### What "silently kills audio" would actually look like

None of the 44 can do it today. The realistic silent-audio failure in
this subsystem is already located and handled: if a *located* setter
were bound to the wrong inner (the §5 trap), volume/frequency/format
would be written to the wrong voice field and streams would play at
wrong pitch/volume with no fault. That is a mis-binding risk, not a
missing-name risk, and §5 is written to prevent it.

## 5. The four-slot `CDirectSoundStream_Set*` cluster — resolved

`docs/analysis-xdk-gaps.md:342-349,405-415` left 0x2A672E, 0x2A677C,
0x2A6E15, 0x2A6E63 as four inseparable 78-byte candidates for nine
missing stream setters. Reading the bodies and their sole callers
separates them completely — and shows they were never stream setters.
There are **five** such bodies, not four (0x2A74DF was missed):

| Slot | Sole caller (DSOUND) | Inner callee | Field written | Identity (buffer-side inner of) |
| --- | --- | --- | --- | --- |
| 0x2A672E (78B, `retn 8`) | `IDirectSoundBuffer_SetVolume` 0x2A68AD @0x2A68C1 | `_sub_002A5D25` (28B) | `[inner+0x1C] = arg − [inner+0x20]`, flush `_sub_002AB6F3` | volume |
| 0x2A677C (78B, `retn 8`) | `IDirectSoundBuffer_SetMixBinVolumes` 0x2A68C9 @0x2A68DD | `_sub_002A5D41` (29B) → `_sub_002A5C2C` | `[inner+bin*4+0x30]` per bin | mix-bin volumes |
| 0x2A6E15 (78B, `retn 8`) | `IDirectSoundBuffer_SetFrequency` 0x2A6F03 @0x2A6F17 | `_sub_002A6CE2` (36B) → `_sub_002A5D0C` | `[inner+0x18]`, flush `_sub_002AB78B` | frequency |
| 0x2A6E63 (78B, `retn 8`) | `IDirectSoundBuffer_SetMixBins` 0x2A6F1F @0x2A6F33 | `_sub_002A6D06` (29B) → `_sub_002A6A68` | bin list at `[inner+0x28…]` | mix bins |
| 0x2A74DF (78B, `retn 8`) | `IDirectSoundBuffer_SetFormat` 0x2A752D @0x2A7541 | `_sub_002A70FC` (68B) | format via `_sub_002A7076`, then mix-bin refresh | format |

The located stream setters are the 82-byte partners that call the
**same** innermost helpers with `this+4` instead of `this`:
`CDirectSoundStream_SetVolume` 0x2A67CA → `_sub_002A5D25(this+4)`,
`CDirectSoundStream_SetMixBinVolumes` 0x2A681C → `_sub_002A5D41(this+4)`,
`CDirectSoundStream_SetMixBins` 0x2A6EB1 → `_sub_002A6D06(this+4)`.
(4-byte difference = the stream object's second vtable word; buffer
wrappers pre-adjust `this−0x1C`, stream wrappers post-adjust `+4`,
both landing on the same voice struct.) Game-side reachability
confirms the split: the buffer wrappers have `.text` callers
(`SetVolume` ×4, `SetFrequency` ×5, …), the stream wrappers only
thunk callers (0x2A699D ×3 `.text`, 0x2A69A2 ×1, 0x2A6F5B ×1).

**Consequences.** (a) The five slots are already identified — as
buffer inners, reachable today only through located, host-replaced
buffer wrappers, so the guest copies are dead code in the port and
must stay unbound. Binding any missing stream name to one of them
(the naive 78-vs-82 length guess) would write the wrong field on the
wrong object adjustment: e.g. binding `CDirectSoundStream_SetFrequency`
to 0x2A6E15 would run the buffer-adjusted frequency inner on a stream
object. **Do not add any of these five addresses to `[symbols]` under
any name.** (b) The nine missing stream setters have no bodies at all:
their innermost helpers mostly exist (the voice core below
`_sub_002A6425`/`_sub_002ACAB2` implements 3D/filter math for
`SetAllParameters`, which itself is xtlid-only here), but the
82/86/90/107-byte critical-section wrappers were dead-stripped with
the exports.

**What evidence would separate stream setters if bodies ever needed
separating** (e.g. in another title where they are linked), in order
of decisiveness: (1) argument count from `retn N` — `SetFilter`/
`SetFormat`/`SetFrequency`/`SetHeadroom` clean 8 (this+1 arg),
`SetI3DL2Source`/`SetMaxDistance`/`SetMinDistance` clean 12,
`SetPosition`/`SetVelocity` clean 20 — already splits 9 into 4/3/2;
(2) innermost callee identity and the field offset it writes
(+0x18 frequency, +0x1C volume, +0x30 bins, 3D block at +0x34/+0x38/
+0x3C for distances/mode — cf. host `BUF(SetAllParameters)`
`dsound.cpp:741-748`); (3) sole-caller identity (buffer wrapper vs
stream thunk vs vtable slot); (4) nested callee-length cluster from
`signatures.toml` (e.g. Max vs Min distance differ only in the
0x33-byte clamp callee hash); (5) neighbour order against located
`SetVolume`/`SetMixBinVolumes`/`SetMixBins`. Name similarity is not
on the list.

## 6. Fixes specified

1. **No `[symbols]` change.** All 44 stay `0` in `config/game.toml`.
   (Contrast the two justified D3D additions in
   `docs/analysis-xdk-gaps.md:332-335`; DirectSound has no equivalent.)
2. **No new host code.** All 44 already have `HLE_` bodies (§2 table).
   For completeness, the host contract each body must keep — verified
   against the guest clusters, calling convention `stdcall`
   (`HLE_STDCALL`, callee cleans `4×nargs`), `this` first stack arg:
   voice setters validate via `Find(a[0])` and return `0x80004005`
   for unknown objects (`dsound.cpp:596-603`); `SetFormat` flushes
   packets before re-reading the format (`:823-829`, mirroring guest
   `_sub_002A70FC`'s teardown-then-set); `Lock` honours
   `DSBLOCK_ENTIREBUFFER` and wrap pointers (`:694-705`, guest
   cluster `[0xE2 core]`); `GetCurrentPosition` reports play cursor
   plus a 256-frame write-ahead cursor (`:684-689`); effect/HRTF/
   filter/I3DL2 setters are deliberate `return 0` no-ops because the
   DSP/filter/reverb is not reproduced (`:493`, `:714`, `:740`,
   `:888`, `:916`, and `D:/Urbz/docs/gap-analysis.md:61-66`). If a
   future change ever *did* need a new body, it must follow this
   pattern — lock `g_lock`, `Find` the voice, never dereference a
   guest inner pointer — because §4 is what the guest body does instead.
3. **Regression watch (the only actionable item).** At boot, confirm
   the log does **not** contain `dsound: exports running their
   original code:` (`AuditExports`, `dsound.cpp:424-473`), which
   fires for any XTLID-linked DSOUND export still running guest code
   on a host object — the exact §4 crash shape. Also watch for
   `dsound: format tag %X not supported` (`:368`): the title's bank
   audio must be PCM or Xbox ADPCM (tag 0x69) for the mixer. Either
   line, not any of the 44 names, is the audio bring-up signal.
4. **Do not "fix" the five §5 slots.** They are identified, dead, and
   correctly unbound; naming them is the one change that could
   introduce a §4 failure where none exists.

## 7. Summary

- 44/44 names: **0 `.text` callers, 0 `DSOUND` callers, no `.XTLID`
  entry, no matching body** — the game does not call any of them
  (§1–§3; independently, Urbz leaves all 44 at 0 on the same XDK).
- 44/44 already host-implemented in `sdk/runtime/xdk/dsound.cpp` (§2);
  nothing needs writing and no `[symbols]` line can be justified.
- Counterfactual risk if a guest body ever ran on a host object:
  35 crash-family (unchecked `[this+0xC/0x10/0x20]` inner dereference;
  2 of those with an APU poll-hang (0xFE820010) behind the crash,
  2 cluster-only `C*`), 8 silent plus 1 UNCONFIRMED `S*`
  (HRTF/effects/speaker surface that is not reproduced by
  design) (§4).
- The four-slot stream cluster is five slots, all separated by sole
  caller + inner callee + field offset: they are buffer-side inners,
  not stream setters, and must stay unbound (§5).
