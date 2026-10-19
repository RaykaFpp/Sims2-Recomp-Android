# DirectSound `.XTLID` exports running guest code — The Sims 2 (USA), XDK 5849

Scope: the five DSOUND exports the boot-time `AuditExports` check
(`sdk/runtime/xdk/dsound.cpp:427-473`) reports as still running their
original code — `0xB00A3`, `0xB00A5`, `0xB00A4`, `0xB0080`, `0xB00A9` —
plus the ~72 s crash whose guest stack ends in that section. This
document supersedes the reachable-set conclusion of
`docs/analysis-dsound.md` (see §6); its per-name host-coverage table
for the 44 `[symbols] = 0` names is not repeated here.

Evidence base: `assets/default.xbe` `.XTLID` section (guest VA
0x401100, file offset 0x33D000, 0x568 bytes), `build/sims2_flat.exe`
in IDA (guest VAs map 1:1) cross-checked with Ghidra via REA,
`sdk/xdk/5849/signatures.toml`, `config/game.toml`,
`D:/Urbz/assets/default.xbe` and `D:/Urbz/config/game.toml` (same XDK
5849 DSOUND library). Section ranges: `.text` 0x11000..0x270880,
`DSOUND` 0x2A5740..0x2AF920.

**Headline result.** None of the five has any entry at all in
`signatures.toml` — there is no recorded `len`, hash, callee cluster
or `xtlid` field for any of them (§2). They are therefore not
"unmatched by bytes"; they are absent from `xbe_symbols`' symbol
universe, which is why no `[symbols]` line exists and why
`AuditExports` fires. Four of the five are provably benign as guest
code. The fifth, `0xB00A9`, creates the guest file-stream object
whose guest destructor is the crash *trigger* (§4) — but the faulting
read is of a corrupted CRT list node, not of a hardware register,
and the corrupting defect is outside DSOUND (§4.3). Official XDK
names for the five could not be verified from any artefact in either
repository; proposed descriptive names are marked **PROPOSED** and
must not be bound to a host body until the name/behaviour contract is
verified (§3).

## 1. The `.XTLID` table, decoded

173 (id, address) pairs, read directly from the XBE. Library id is
the high field (`id >> 16`): 0x1 XAPI/kernel-adjacent, 0x2 kernel,
0x8 D3D, 0xB DSOUND, 0xD XGRPH, 0xE XMV. The five complained-of ids
are present and map exactly where the runtime log said:

| id | guest address | in `config/game.toml`? |
| --- | --- | --- |
| 0xB00A3 | 0x2A5A36 | no name exists |
| 0xB00A5 | 0x2A5A80 | no name exists |
| 0xB00A4 | 0x2A5A85 | no name exists |
| 0xB0080 | 0x2A69AC | no name exists |
| 0xB00A9 | 0x2A69ED | no name exists |

The 43 DSOUND-library (0xB) entries, in table order:
0xB0045→0x2A5A0A, 0xB005D→0x2A5A20, **0xB00A3→0x2A5A36**,
**0xB00A5→0x2A5A80**, **0xB00A4→0x2A5A85**, 0xB003C→0x2A686E,
0xB0051→0x2A6895, 0xB007C→0x2A68AD, 0xB0072→0x2A68C9,
0xB0059→0x2A68E5, 0xB005A→0x2A6909, 0xB007D→0x2A692D,
0xB0057→0x2A6945, 0xB006E→0x2A6961, 0xB0054→0x2A6981,
0xB009E→0x2A699D, 0xB0096→0x2A69A2, 0xB0082→0x2A69A7,
**0xB0080→0x2A69AC**, 0xB000F→0x2A69C4, **0xB00A9→0x2A69ED**,
0xB006A→0x2A6F03, 0xB0071→0x2A6F1F, 0xB0077→0x2A6F3B,
0xB0095→0x2A6F5B, 0xB0069→0x2A752D, 0xB0060→0x2A7549,
0xB003A→0x2A7AEE, 0xB0002→0x2A7CB8, 0xB0004→0x2A7CFF,
0xB001A→0x2AED6C, 0xB0007→0x2AEDA0, 0xB0008→0x2AEDE0,
0xB0006→0x2AEE08, 0xB0009→0x2AEE54, 0xB002D→0x2AEE5C,
0xB000C→0x2AEE64, 0xB000A→0x2AEE6C, 0xB000E→0x2AEE74,
0xB000D→0x2AEE74, 0xB0034→0x2AF064, 0xB0033→0x2AF068,
0xB0035→0x2AF078. Every entry except the five resolves to a name
already in `config/game.toml` (e.g. 0xB0002 `DirectSoundCreate`
0x2A7CB8, 0xB003C `IDirectSound_DownloadEffectsImage` 0x2A686E) or
to the decoder tail at 0x2AED6C+ that `AuditExports` deliberately
excludes (it only reports ids whose address lies in the section
containing `DirectSoundCreate`). The full 173-entry table is
Appendix A.

Urbz (same library, different link base) has the identical five, at
0xB00A3→0x27ED56, 0xB00A5→0x27EDA0, 0xB00A4→0x27EDA5,
0xB0080→0x27FCCC, 0xB00A9→0x27FD0D — matching
`D:/Urbz/docs/status.md:105-109`, which also leaves them unnamed.
Bodies at those addresses are the same code (same instruction
shapes/lengths verified for the two format builders and the create
helper); no Urbz `[symbols]` name exists for any of them either.

## 2. The five bodies, and why `xbe_symbols` has no name for them

`signatures.toml` contains 108 `xtlid` fields; the values
0xB00A3, 0xB00A5, 0xB00A4, 0xB0080, 0xB00A9 occur **nowhere** in the
file (nor in the Urbz copy). So for each of the five the requested
comparison "recorded `len`/cluster vs actual body" has no recorded
side: the matcher did not reject these bodies, it never had a name
to match them against. What follows is the actual body, measured in
IDA, with its callee cluster measured the same way.

### 0xB00A3 → 0x2A5A36 — sample-rate → pitch converter (74 bytes)

`retn 4`, one stack argument. If the rate is 48000 (0xBB80) it
returns 0; otherwise it returns `(int)(4096 * log2(rate / 48000))`
— the float constant at 0x2F1AE4 is 1/48000, the callee at
0x2AA042 (30 bytes) computes `fyl2x` against 4096.0 and rounds via
`fistp`, bracketed by the TLS-guard helpers at 0x2A57A9 (37 bytes)
and 0x2A57CE (30 bytes). Callees: [0x25 guard, 0x1E log2, 0x1E
guard]. Its only callers are DSOUND-internal: 0x2A6CE2 (the
frequency inner behind `IDirectSoundBuffer_SetFrequency`) and
0x2A7076 (the format inner behind `IDirectSoundBuffer_SetFormat`
0x2A752D), both of which store its result as the voice pitch at
`[voice+0x18]`. It takes no `this`, dereferences nothing, and in
this port is reachable only from guest inners that are themselves
dead (their wrappers are host-replaced). Neighbour order confirms
the slot: it sits immediately after `IDirectSound_Release`
(0x2A5A0A, 0xB0045) and `IDirectSoundBuffer_Release` (0x2A5A20,
0xB005D) in both images. **PROPOSED name:**
`DirectSoundFrequencyToPitch` — descriptive only, unverified.

### 0xB00A5 → 0x2A5A80 and 0xB00A4 → 0x2A5A85 — WAVEFORMATEX builders

Each is a 5-byte `jmp` thunk (the adjustor-thunk pattern of §5 of
the earlier document, here forwarding without adjustment):
0x2A5A80 → 0x2A7EB8 (74 bytes), 0x2A5A85 → 0x2A7F02 (67 bytes).

- 0x2A7EB8 writes a PCM `WAVEFORMATEX` (`wFormatTag = 1`): channels
  from arg0 → +2, samples/sec from arg4 → +4, bits from arg8 →
  +0xE, computed block-align → +0xC, byte-rate → +8, `cbSize = 0`.
  `retn 0x10` (3 args + out pointer).
- 0x2A7F02 writes an Xbox ADPCM `WAVEFORMATEX` (`wFormatTag =
  0x69`): channels → +2, rate → +4, byte-rate → +8, block-align →
  +0xC, 4 bits → +0xE, `cbSize = 2` → +0x10, 0x40 → +0x12.
  `retn 0xC`.

Both are pure struct writers: no `this`, no global state, no
hardware access. Game callers: 0x2A5A80 ×1 and 0x2A5A85 ×1 from
game stream setup `_sub_00212270` (§5), 0x2A5A85 ×1 from
`_sub_00211C50`. Running the guest bodies is safe and is in fact
what happens today — the host's `DirectSoundCreateStream`
replacement consumes the format they write. **PROPOSED names:**
`DirectSoundBuildPcmWaveFormatEx` /
`DirectSoundBuildAdpcmWaveFormatEx` — descriptive only, unverified.

### 0xB0080 → 0x2A69AC — FlushEx forwarder (24 bytes)

Pushes its four stack arguments unchanged and calls
`CDirectSoundStream_FlushEx` (0x2A63BB, 106 bytes, host-replaced),
`retn 0x10`. It is the COM-interface-shaped twin of the located
stream thunks beside it (0x2A699D `SetVolume`, 0xB009E; 0x2A69A2,
0xB0096; 0x2A69A7 `Pause`, 0xB0082). Its only caller is in the XMV
section: `_sub_002AFC37` (XTLID 0xE0001) at 0x2AFC64, which flushes
each of its streams with `(stream, 0, 0, 1)` during WMA teardown.
Guest execution is a pure forward into the host body; it touches
no object field itself. **PROPOSED name:**
`IDirectSoundStream_FlushEx` — by analogy with the verified
`IDirectSoundStream_SetEG` (0xB008C) naming split, unverified.

### 0xB00A9 → 0x2A69ED — guest stream-source create (96 bytes)

Allocates 0x44 bytes, constructs the object (ctor 0x2ACE64,
68 bytes: vtables 0x2F1C2C / 0x2F1C1C, refcount 1 at +8, embedded
file object at +0xC, critical section at +0x24, packet array at
+0x40), initialises it (0x2ACBE1, 162 bytes: packet count from
arg, one event per 0x30-byte packet), AddRefs it via the vtable-0
forwarder at 0x2A6706, and stores it through the out pointer.
Callees: [alloc, 0x44 ctor, 0xA2 init, 0x14 AddRef-forwarder].
Sole caller: game stream setup `_sub_00212270` at 0x2123F6 (§5).
This is the only one of the five that manufactures a guest object
the game later drives through guest vtable slots — and the only
one on the crash path. **PROPOSED name:**
`DirectSoundCreateStreamSource` — descriptive only, unverified.
Its class has no name in either repository; its vtable (0x2F1C2C)
slots are: +0x00 AddRef (0x2ACEA8), +0x04 Release-adjustor
(0x2ACEB5), +0x08 reset (0x2ACC83), +0x0C status (0x2ACEF1),
+0x10 process/submit (0x2ACF86, 354 bytes), +0x14 no-op
(0x2ACEC5), +0x18 flush/cancel (0x2ACCE1, 166 bytes), +0x1C
seek/position (0x2ACC9B), +0x20 length (0x2ACECA), +0x24 status
(0x2ACDBC), +0x28 deleting destructor (0x2ADFFF).

## 3. `[symbols]` lines

Format follows `config/game.toml` (name column padded as in the
file). These are proposals under the **PROPOSED** names of §2; the
addresses are certain, the names are not verified XDK symbols.
Binding any of them additionally requires a `signatures.toml` entry
and, where a host body is wanted, an `HLE_` registration under the
same string — a `[symbols]` line alone, under a name the host does
not implement, replaces nothing. Recommendations differ per line:

```
DirectSoundFrequencyToPitch                          = 0x2A5A36
DirectSoundBuildPcmWaveFormatEx                      = 0x2A5A80
DirectSoundBuildAdpcmWaveFormatEx                    = 0x2A5A85
IDirectSoundStream_FlushEx                           = 0x2A69AC
DirectSoundCreateStreamSource                        = 0x2A69ED
```

Evidence per line, in the order (xtlid, length, callee cluster,
neighbour order, sibling port):

- `DirectSoundFrequencyToPitch = 0x2A5A36` — xtlid 0xB00A3 in both
  XBEs; 74-byte body, cluster [37B TLS guard, 30B `4096*log2`, 30B
  guard]; sits between located `IDirectSoundBuffer_Release`
  (0x2A5A20) and the 0x2A5A80 thunk with no unclaimed slot;
  Urbz 0x27ED56. Do not host-replace: unreachable in this port
  (guest-internal callers only) and harmless if reached.
- `DirectSoundBuildPcmWaveFormatEx = 0x2A5A80` — xtlid 0xB00A5;
  5-byte thunk to the 74-byte PCM builder; game caller
  `_sub_00212270`; Urbz 0x27EDA0. Safe as guest code; replace only
  to silence the audit, with a body writing the identical 18-byte
  format.
- `DirectSoundBuildAdpcmWaveFormatEx = 0x2A5A85` — xtlid 0xB00A4;
  5-byte thunk to the 67-byte tag-0x69 builder; game callers
  `_sub_00212270`, `_sub_00211C50`; Urbz 0x27EDA5. Same disposition.
- `IDirectSoundStream_FlushEx = 0x2A69AC` — xtlid 0xB0080;
  24-byte forwarder, cluster [106B `CDirectSoundStream_FlushEx`];
  between located thunks 0x2A69A7 (0xB0082) and `DirectSoundDoWork`
  (0x2A69C4, 0xB000F); Urbz 0x27FCCC. Safe as guest code (pure
  forward to the host body).
- `DirectSoundCreateStreamSource = 0x2A69ED` — xtlid 0xB00A9;
  96-byte create, cluster [0x44 ctor, 162B init, 20B AddRef
  forwarder]; sole game caller `_sub_00212270` at 0x2123F6;
  Urbz 0x27FD0D. **Not recommended for host replacement under this
  unverified name** (§4.3): if it is ever replaced, the replacement
  must honour the vtable contract of §5, not return success inertly.

Unresolved, deliberately not guessed: the official XDK symbol
strings for all five. No artefact in `D:/sims2` or `D:/Urbz`
contains them (`signatures.toml` in both trees knows 108 xtlids,
none of these), and a wrong name silently binds nothing — safer to
record the addresses under descriptive names than to invent an
official-looking one.

## 4. The faulting function

### 4.1 The chain

The four DSOUND return addresses are one chain, verified
instruction by instruction:

- 0x2ACEC2 is the return address of `call _sub_002A580A` inside the
  Release adjustor at 0x2ACEB5 (vtable 0x2F1C2C slot +0x04, entered
  with `this+4`).
- 0x2A5821 is the return address of `call dword ptr [eax+0xC]`
  inside the dec-ref helper at 0x2A580A: when the refcount at
  `[this+4]` reaches zero it invokes vtable slot +0x0C.
- 0x2A5807 is the return address of `call dword ptr [eax]` inside
  0x2A57F9, which zeroes the refcount and invokes vtable slot
  +0x00 with deleting flag 1.
- 0x2AD0F0 is the return address of `call _sub_002ACF21` inside the
  deleting destructor at 0x2AD0E8 (entered from the `sub ecx,4`
  thunk at 0x263862, vtable 0x2F1C1C slot +0x00).

So the game released the 0xB00A9-created source object, the guest
dec-ref ran, and the guest destructor at **0x2ACF21** (the class
destructor; it re-stamps the vtables, cancels/flushes via
0x2ACCE1, closes each packet's handle, frees the packet array,
closes the embedded file object via 0x2A8003) is the faulting
*frame*. Inside it, 0x2ACCE1 (flush/cancel, 166 bytes) first —
because the embedded file object's flags at `[obj+0x10]` have bit
1 set (set by 0x2ACBE1 at creation) and `[obj+0x18]` is nonzero —
calls the CRT stream helper at **0x265696** (in `.text`, XTLID
0x2013D) with the embedded file handle.

### 4.2 The faulting instruction and what 0xFF000024 is

0x265696 walks the guest CRT's open-stream list, anchored at
`[dword_2DEEA0 + 0x28]` (the same anchor whose +0x10/+0x14 slots
hold the atexit handlers installed by 0x2201DA). Each list node is
a stream control block with the `FILE` at +0x3C; the loop at
0x2656E1 loads the node pointer into `ecx`, sets
`esi = ecx - 0x3C`, and reads **`mov edx, [esi+0x60]` at
0x2656E4** — i.e. `[node + 0x24]`, the field it compares against
the stream being closed. With the node pointer equal to
0xFF000000, that read is guest 0xFF000024. This is the only
`[base+0x24]` read in the entire reachable set from 0x2ACF21
(direct callees 0x2ACCE1, 0x2A8003, allocator/free paths, and the
vtable-resolved callees 0x2ACC9B → 0x2A8136/0x2A81B1 were all
disassembled; Ghidra pseudocode for 0x2ACF21/0x2ACCE1/0x2ACC9B/
0x2A8003 via REA agrees).

0xFF000024 is therefore **not a hardware register**. The console's
APU register windows used by this library are at 0xFE801000 and
0xFE820000 (dozens of `ds:0FE82xxxx` accesses in
0x2A87EA..0x2AE087, including the FIFO poll at 0x2AB722); no
instruction in the DSOUND section references a 0xFF000000-based
window, and no dword 0xFF000000 is stored anywhere in the image.
On the console the list node is an ordinary RAM pointer and the
read is unremarkable. The value 0xFF000000 in that slot means the
guest CRT stream list being walked was corrupted or never validly
linked in the port — the destructor is the *trigger* that walks
it, not the corrupting defect. (Live-side confirmation of where
the corruption originates is the sibling agent's minidump/watchdog
evidence, outside this document's static scope.)

### 4.3 Which export to replace, if any

The faulting bodies (0x2ACF21, 0x2ACCE1, 0x265696) belong to no
XDK export; they are the 0xB00A9 class's destructor, its
flush/cancel method, and a CRT helper. The nearest enclosing
export the host *could* replace to keep the guest out of all of
them is the create itself, **0xB00A9 at 0x2A69ED**: a host create
returning a host-owned source object would make the game's
Release land in host code. That is also precisely why it must not
be done casually — §5 shows the game actively drives the object
(Process, seek, status, Release) to feed the host
`CDirectSoundStream`, so an inert replacement trades this crash
for silent audio. Given §4.2, the correct disposition is to fix
the corruption that puts 0xFF000000 in the CRT list and leave
0xB00A9 guest; host-replacing it under an unverified name is
recorded here as evaluated and rejected.

## 5. The game-side call sites

`_sub_00212270` (game stream setup, called from `_sub_002129B0`)
opens one audio channel (the `esi` struct, 0x58 bytes):

1. It builds a `WAVEFORMATEX` on the stack with the guest
   builders — PCM via 0x2A5A80 if the bank entry's flags are
   aligned one way, Xbox ADPCM via 0x2A5A85 the other — extracting
   channels/rate from the bank descriptor.
2. It calls the host-replaced `DirectSoundCreateStream`
   (0x2A7CFF) with a descriptor pointing at that format, storing
   the `CDirectSoundStream` at channel+4, then drives it through
   the located thunks: `SetMixBins` (0x2A6F5B), `SetMixBinVolumes`
   (0x2A69A2), `SetVolume` (0x2A699D).
3. It calls **0x2A69ED (0xB00A9)** with `(bank source, 2,
   channel)` — creating the guest file-source object with two
   overlapped packets, stored at channel+0.
4. It opens the bank file (0x21D0FE) into channel+0x14 and calls
   the source's vtable slot +0x1C (0x2ACC9B, seek/position) with
   the channel's end offset.

Teardown is `_sub_00211FF0` (called from `_sub_001E8A88`), which
releases the bank object and calls `_sub_00211AB0`; that function
releases channel+0 through vtable slot +0x04 — the guest
0x2ACEB5 adjustor, producing return address **0x211AC3** — then
releases channel+4 (the host stream, safe), then frees the file
handles at +0x14/+0x30. Return address **0x212046** is
`_sub_00211FF0`'s call site for `_sub_00211AB0`. So both `.text`
frames are the channel-destroy path: the game is closing a
streaming voice at bank/channel teardown, and the first Release
is what enters the guest destructor chain of §4.

Consequence for any future host replacement of 0xB00A9: the game
uses the source object's Process (slot +0x10), seek (slot +0x1C),
status (slots +0x0C/+0x24) and Release (slot +0x04) to move file
data into the host stream's packets. A replacement must deliver
real data through that contract; it may not be inert.

## 6. Re-audit of `docs/analysis-dsound.`
...[truncated 6120 chars]