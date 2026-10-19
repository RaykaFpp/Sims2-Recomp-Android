# Startup analysis — entry to title screen (The Sims 2 USA, XDK 5849)

All addresses are guest VAs in `build/sims2_flat.exe` (1:1). Excerpts are IDA
pseudocode/disassembly from that image. Anything not proven is marked
**UNCONFIRMED**.

## 1. Boot path: entry → render loop → Reset

Entry `0x0021BF66` (`config/game.toml: entry = 0x21BF66`):

```c
// _sub_0021BF66 @ 0x21bf66
v1 = sub_0021D02B(0,0, sub_0021BEF2, 0,0,0);   // create main thread
if (v1 == 0) sub_0021C6A2(1,1,0);
return sub_0021BBD8(v1);
```

Main thread proc `0x0021BEF2`:

```c
sub_0021CCB2(); sub_0021C718(); ... sub_0021CC25(); sub_0021CBCD();
sub_001F34F0(0);            // game main
sub_0021C6A2(1,1,0);
```

`sub_001F34F0@0x1F34F0` is a two-call wrapper:

```c
int sub_001F34F0(int a1){ sub_001F3390(a1); sub_001F3490(); return 0; }
```

`sub_001F3390@0x1F3390` parses the command line (`sub_0021D401`), creates the
application object (`sub_001BD930(dword_3B80E8, argc, argv)`), then registers
the idle/task object at `off_33E28C`:

```c
v7 = sub_0021D099();
dword_33E290 = -2; sub_001E5870(&off_33E28C);
dword_33E294 = v7; dword_33E298 = 0; dword_33E2A8 = dword_3EB264; ...
Idle = "Idle";
```

`sub_001F3490@0x1F3490` enters the scheduler:

```c
v0 = dword_3B80E8;
v1 = (*(int(**)(void))(*dword_3B80E8 + 96))();
sub_001E58E0(3, v1);                       // enqueue task, priority 3
*(_DWORD*)(v0+24) = "Application";
sub_0021CE6E(*(_DWORD*)(v0+4));
sub_0021CDB0(dword_33E290, -15);
(*(void(**)(int))(*dword_3B8130 + 32))(dword_3B8130);
sub_001F92E0();
if (dword_3B81D4) dword_3B81D4();
```

The already-observed chain `sub_0021CF93 → sub_001E5960 → sub_001FC040`
is the task trampoline: `sub_001E5960@0x1E5960` is
`(*(void(**)(int))(*a1+4))(a1); return 0;` — it invokes vtable slot +4 of the
task object, which is `sub_001FC040`.

### The frame/task loop that owns presentation

`sub_001FC040@0x1FC040` (`__noreturn`) is the render/task pump:

```c
v3 = this + 13;
while (1) {
  if ((*(u8(**)(_DWORD*,int))(v3[3]+4))(v3+3, -1) != 0) {   // queue non-empty?
    (*(void(**)(_DWORD*,int))(v3[13]+4))(v3+13, -1);         // lock
    v4 = v3[7];
    this_2 = *(_DWORD**)(v3[9] + 4*v4);                      // dequeue task
    v3[7] = (v4+1) % v3[8];                                  // ring index/count
    (*(void(**)(_DWORD*))(v3[13]+12))(v3+13);                // unlock
    (*(void(**)(_DWORD*))(*v3+12))(v3);
  }
  sub_001FC0E0(this_2, this);      // dispatch one task by opcode
  sub_001B55A0();
  if (this_2) { ... sub_001B6520/sub_001B6200/sub_001F4110 (refcount/free) }
}
```

State storage: the queue lives inside the renderer object at `this+0x34`
(`this+13` dwords): `+0x40` vtable/queue object (`v3[3]`), `+0x48` lock
object (`v3[13]` in the +4/+12 calls above after rebasing), `+0x58` task
pointer array (`v3[9]`), `+0x4C/+0x50` head index and capacity (`v3[7]/v3[8]`).
**UNCONFIRMED:** exact field names; offsets above are as decompiled.

The advancing function is `sub_001FC0E0@0x1FC0E0`, a switch on the task's
first dword (`*n2`). This is a *render-command* state machine, not the
logo/movie/title sequencer (see §1.1):

| `*task` | Handler at 0x1FC0E0 | Meaning (from handler body) |
|---|---|---|
| 1 | `sub_001FC430` | Clear backbuffer (`D3DDevice_Clear(...,243,...)`) |
| 2 | `sub_001FC4F0` → `sub_001FC9D0(this)` | Flush fence/texture free lists |
| 3 | `sub_00275750(1, sub_001FA8C0, *(task+4))` if non-null | Kick GPU work |
| 4 | `sub_001FC550(this)` | `SetRenderTarget`, `InsertFence`, `D3DDevice_Swap(0)` — **present** |
| 5 | `sub_001FCB00(this)` → `sub_001FC740` | See below — mode/present path |
| 6 | inline | If `*(task+8)`: `D3DDevice_BlockUntilVerticalBlank()`; if `*(task+4)`∈{1,2} and differs from `this[9114]`, store it and call `sub_00271570(this+9102)` — **D3DDevice_Reset** |
| 7 | `sub_001FC1C0` | Enqueue into list at `this+8704` |
| 8 | `sub_001FC240` | Enqueue into list at `this+4604` |
| 9 | `sub_001FC340` | **UNCONFIRMED** (not read) |
| 10 | `sub_001FC3C0(*(task+4))` | **UNCONFIRMED** |
| 11 | `sub_001FC2C0` | **UNCONFIRMED** |

The observed `… → sub_001FCB00 → D3DDevice_Reset (0x271570)` is state 5→6:

```c
// sub_001FCB00 @ 0x1fcb00
int sub_001FCB00(int this@<eax>){
  sub_00275750(1, sub_001FA8C0, this+128);
  return sub_00271290();                 // D3DDevice_Swap tail
}
// sub_001FC0E0 case 5: sub_001FCB00(this); n2 = sub_001FC740();
// sub_001FC0E0 case 6 (excerpt):
if (*(n2+8)) D3DDevice_BlockUntilVerticalBlank();
n2 = 1; if (n2_1==1 || (n2=2, n2_1==2))
  if (*(this+9114) != n2){ *(this+9114)=n2; n2 = sub_00271570(this+9102); }
```

So the presentation-parameters block fed to Reset is at `renderer+9102*4`
(`this+0x8E78` approx. — decompiler dword indexing), and the last-applied
mode index is `renderer[9114]`. The fatal screen (§4) builds that same block
on its stack (`v7[0]=640, v7[1]=480, v7[2]=30, v7[4]=17, v7[5]=3`) and calls
`sub_00271570(v7)` directly — hence the log ambiguity noted in status.md.

Whole dispatch is gated: `if (byte_3B81E0 == 0) { switch… }` in
`sub_001FC0E0`. `byte_3B81E0` is set to 1 by the fatal screen (its first
statement). Once set, the pump still loops but dispatches nothing.

### 1.1 The logos → movies → title sequencer

**UNCONFIRMED in full.** What is established:

- The movie is a renderable object whose vtable is built by
  `sub_0026F1D0@0x26F1D0`; it stores `sub_001FF630_0 = sub_001FF630` with a
  trailing type tag `n2_6 = 2`. Neighbour slots (`sub_001FF620`, `sub_001FF890`
  tag 3, etc.) are the object's other methods. The vtable base used at
  runtime is in the `0x33E3xx–0x33E5xx` global table.
- Movie playback state lives in the *movie/player object* (the `a1` of
  `sub_00219EE0`, §3) at `+16`: set to `2` before open, to `1` on successful
  open. **No other writer of `+16` was identified.**
- The front-end (Apt/Flash) names for the destination exist as strings only:
  `MainMenu@0x2F8310`, `mainmenu@0x2F8B70`, `MMU_Main_Menu_*@0x3020F8…`,
  `music_48_title_track@0x2F88B0`. No guest function referencing
  `skip_startup_movies` by name-hash was found (§2), and no explicit
  "state = LOGO/ATTRACT/TITLE" enum was located. The sequencer that steps
  logo → EA movie → intro → attract → title is therefore **UNCONFIRMED**;
  the cheapest way to find it is §5, H3's observation (log the caller chain
  of `sub_00219EE0`'s vtable slot at file offset `0x315F90`-ish — the sole
  OFFSET xref to `sub_00219EE0`).

## 2. `skip_startup_movies`

String: `skip_startup_movies@0x2F4B24`, help text at `0x2F4B38`:
*"If enabled the intro movies won't be played"*.

Registered once, in the cheat/settings registrar `sub_0002A300@0x2A300`
(called by `sub_00043BE0@0x43BE0`, itself called by `sub_00043250@0x43250`):

```asm
; 0x2c3e2  (inside sub_0002A300)
lea     edx, [ebx+381h]              ; ebx = settings object (a1)
push    edx                          ; value pointer  = settings+0x381
push    2                            ; type 2 (bool)
mov     edi, offset aIfEnabledTheIn  ; "If enabled the intro movies won't be pl..."
mov     ecx, offset aSkipStartupMov  ; "skip_startup_movies"
call    _sub_0002CAC0                ; register(name, help, type, &value, ...)
```

`sub_0002CAC0@0x2CAC0` stores: `*(desc+80) = value_ptr`, `*(desc+76)=type`,
name at `desc+8`. So the variable is **one byte at `settings+0x381`
(= +897)**. Default is 0: the settings constructor `sub_00042E00@0x42E00`
does `memset(a2+352,0,0x224)` then `*(_BYTE*)(a2+897) = 0;`.

Who reads it: the only other `+0x381` byte read found by scanning the image
for displacement `81 03 00 00` is in `sub_0003A430@0x3A430`:

```c
v3 = *(_DWORD*)(a2 + 36);            // settings/game object
if (*(_BYTE*)(v3 + 1266) != 0) {     // +0x4F2 gate (UNCONFIRMED meaning)
  (*(void(**)(int,_BYTE*))(*v3 + 132))(v3, v8);
  v9 = *(_DWORD*)(v3 + 280);
  if (*(_BYTE*)(v3 + 897) != 0) {    // <-- skip_startup_movies
    sub_001D1060(v8);
    v4 = *(_DWORD*)(v3 + 840); v5 = v4 ? *(_DWORD*)(v4+24) : 0;
    sub_001E28B0(v9, a1[38], *(v3+820), *(v3+816), v5);
  }
  ... // then vtable +192/+100 dispatch regardless
}
```

**UNCONFIRMED:** that `sub_0003A430` is *the* startup-movie gate (it is
installed as a callback at `obj+1060` by `sub_00037E90@0x37E90`, chosen over
`sub_0003A270` when `(*this+172)()==0`). Its body does not itself open or
skip a movie in the excerpt — when the flag is on it additionally runs
`sub_001D1060`+`sub_001E28B0` (a list/register walk). It is the sole static
reader of the byte, so if the flag works at all, it works through here or
through a reader that accesses the byte with a different displacement form
(not found).

What changes when on: **exactly** the two calls above are added; nothing in
the excerpt branches *away* from movie playback. That is consistent with
"the intro movies won't be played" being implemented inside
`sub_001E28B0`'s callee chain **(UNCONFIRMED)** or with the flag being
consulted again deeper **(UNCONFIRMED — no second reader found)**.

### Would forcing it from the host work?

- Writing the guest byte (`settings+0x381 = 1`) once at boot: legitimate
  *if* done after `sub_00042E00` initialises the object and before the
  `sub_0003A430` callback fires. The engine then follows its own path; no
  state machine is left inconsistent, because the flag is a plain input the
  engine itself sets via the cheat console. Risk: identifying the live
  settings-object address from the host (it is `*(a2+36)` of the callback's
  owner, ultimately the object built by `sub_00043250`'s caller —
  **UNCONFIRMED address**; do not hard-code a global).
- A `[[hook]]`/`X86_FUNC` override of `sub_0003A430` or of the cheat-getter:
  **not** equivalent unless it reproduces the `sub_001D1060`/`sub_001E28B0`
  side effects; a hook that merely returns "skipped" would bypass whatever
  registration those calls perform and could leave the front-end waiting
  for a movie-finished event that never comes (**UNCONFIRMED**, but the
  shape of the code — flag *adds* work rather than *removing* a call —
  argues the skip is a by-product of that work, not an early-out).
- Recommendation: prefer the guest-byte write, timed after settings
  construction; verify with the §5 H3 observation.

## 3. Guest XMV player (section `XMV` 0x2AF920–0x2D73C0)

### Functions the game calls (all guest, none host-replaced)

From `sub_00219EE0@0x219EE0` (movie open, outside the XMV section) and
`sub_001FF630` (present):

| Guest addr | Role | Called from |
|---|---|---|
| `sub_002AF920` | XMV create/decoder init (4 = ? , src header, callbacks, out handle) | `sub_00219EE0` |
| `sub_002AFC37` | XMV destroy (flush streams, close file handles at +31/+4/+3) | `sub_00219EE0` (re-open), **UNCONFIRMED elsewhere** |
| `sub_002AFD1B` | Get dimensions: `a2[0]=a1[16], a2[1]=a1[17], a2[3]=a1[18]` | `sub_00219EE0` |
| `sub_002AFD88` | Seek/reset (0,0,0,0) if `player+32 != 0` | `sub_00219EE0` |
| `sub_002AFF8D` | **Get frame** (see below) | `sub_0021A5F0@0x21A5F0` only |
| `sub_002B002D` | Decode one frame (tail of get-frame) | `sub_002B002A` shim |
| `sub_00219BC0`, `sub_00219C50` | Header read / audio-stream refill helpers (outside XMV) | `sub_00219EE0`, `sub_0021A630` |

Already named in `config/game.toml` `[symbols]`: **none in
0x2AF920–0x2D73C0.** The grep for `0x2Axxxx/0x2Bxxxx` returns only
DirectSound symbols (`0x2A5A0A…0x2A7CFF`, DSOUND section). Every `XMV*`/
movie entry the title uses is recompiled guest code; the SDK's Bink path
(`Bink* = 0`) is unused, as status.md states.

### Open sequence (`sub_00219EE0`, pseudocode "Uflattening failed" — treat flow with care)

1. Pick filename by locale key from the table at `0x315FC4…0x3160D4`
   (`s2c_intro*.xmv`, `s2c_attract*.xmv`, `s2c_ending*.xmv`,
   `ea_games*.xmv`; default `ea_games.xmv`).
2. Tear down previous: `sub_002AFC37(old)` if `player+12`, close handle
   `player+64` via `sub_0021BBD8` (CloseHandle wrapper).
3. `*(_DWORD*)(a1+16) = 2` (state: opening/failed), zero `+88/+92`.
4. Build path `movies/<name>` (`sub_001B6AC0/sub_001B6CF0`, `\` fixup),
   open with `sub_0021BC1C(path, 0x80000000, 1, 0, 3, …)` → `player+64`;
   on `-1` return 0 (caller decides fatality — see §4: open failure does
   **not** itself call `sub_001F7EE0` here).
5. Allocate 4096-byte header buffer (`sub_001B5FE0(4096,4096,0,0)`) →
   `player+96/+48`; `sub_00219BC0(0,0,&hdr)` parses it.
6. `sub_002AF920(4, hdr, a1+48, sub_0021A670, sub_0021A630, a1+12)` creates
   the decoder; callbacks stored at decoder `+28` (read-data) and `+32`
   (seek/refill), context at `+36`. On `<0` return 0.
7. `*(_DWORD*)(a1+16) = 1` (playing). Copy header, set ring:
   `player+60 = hdr_len`, `player+48 = buf`, `player+56 = buf+len`,
   `player+52 = 0`; `sub_00219C50(0,0,hdr)` primes audio.
8. `sub_002AFD1B(handle, a1+20)` fills width/height/count at `+20/+24/+28`;
   optional `sub_002AFD88` seek; create the DirectSound stream via vtable
   `+240/+56/+68` on `dword_35B668` if `player+40 == 0`.
9. `sub_001FC010(dword_3B81FC, 1, 1)` — notify renderer (**UNCONFIRMED**).

### Get-frame / decode (`sub_002AFF8D@0x2AFF8D`)

```c
*a3 = 3;                                    // default: no frame
if (*(a1+108)==0 || *(a1+96)!=0) goto no_frame;      // 0x2B0171
if (*(a1+80)==0) {
  if (*(a1+104) && *(a1+116)) { *a3 = 2; return …; } // buffered frame ready
  goto no_frame;
}
if ((*(cdecl**)(a1+28))(*(a1+36), &v11, v7) < 0) return …;  // read-data cb
if (v11 == 0) goto no_frame;
*(a1+320) = *(a1+332); *(a1+332)=0; *(a1+348)=0;
return sub_002B002A(a1,a2,a3,a4);           // decode
```

Caller wrapper `sub_0021A5F0@0x21A5F0` (holds the movie lock at `a1+100`,
vtable +4 lock / +12 unlock):

```c
v4 = false;
if (a1[3] != 0) v4 = sub_002AFF8D(a1[3], a1[10], a1+4, 0) >= 0;
return v4;
```

And the sole caller, present path `sub_001FF630@0x1FF630`:

```c
if ((u8)sub_0021A5F0() == 0) sub_001F7EE0();   // get-frame failed → fatal
... build fullscreen quad from v2[5]/v2[6] ...
return D3DDevice_DrawVerticesUP(8, 4, v22, 24);
```

So: **one failed get-frame is immediately fatal.** There is no retry, no
"wait for next frame" branch in the present path. `*a3` status: `3` = no
frame (initial), `2` = frame already buffered. **UNCONFIRMED:** whether
status 3 vs a negative return is what `sub_0021A5F0`'s `>= 0` test treats
as failure — get-frame appears to return the status via `a3`, and its own
return on the `no_frame` path is via `JUMPOUT(0x2B0171)` (**not read**).

### Threading and inter-frame wait

- Decoding is **synchronous on the calling (render) thread**: get-frame →
  read-data callback → `sub_002B002D` decode, all inline. No `CreateThread`
  call was found in the XMV functions read (`sub_002AF920` only lazily
  creates a global via `sub_00259B30(&qword_2D7398, …, 1000, 0)` — that is
  the performance-counter frequency init, see below, not a thread).
  **UNCONFIRMED:** exhaustive — the whole 162 KB section was not scanned
  function-by-function.
- The read-data callback `sub_0021A670@0x21A670` performs an overlapped
  file read: it issues/queries `sub_0021C045(handle, player+5*4, &out, 0)`.
  `sub_0021C045@0x21C045` is the overlapped-completion poll:

```c
if (*a2 == 259) {                    // STATUS_PENDING (0x103)
  if (a4) { v4=a2[4]?a2[4]:a1; n258 = sub_0021B22C(v4, -1); }  // wait -1 = INFINITE?
  else n258 = 258;
  if (n258 == 258) { sub_0021C000(996); return 0; }  // still pending → fail
  if (n258 != 0) return 0;
}
*a3 = a2[1]; return (*a2 >= 0) ? 1 : (sub_0021C02E(*a2), 0);
```

  In get-frame's callback invocation `a4` (the wait flag) comes from the
  decoder's call site — **UNCONFIRMED** whether it passes wait=1. If it
  passes 0, a pending disc read makes get-frame *fail* (→ fatal screen),
  not block. Error 996 (`ERROR_IO_INCOMPLETE`-style) and 997 are the
  pending codes treated specially in `sub_0021A670`/`sub_002127B0`.
- The seek/refill callback `sub_0021A630@0x21A630` just forwards to
  `sub_00219C50(a2, a3, a4)` after bounds-checking `a4 <= a1[3]`.
- Timing/pacing: `sub_002B002D` computes presentation time from
  `sub_00259B30` (the `QueryPerformanceCounter`-family wrapper; the lazy
  global at `0x2D7398` is initialised with constant `1000`) and
  `sub_0021BECD@0x21BECD` (`*a1 = __rdtsc()`). It also calls
  `DirectSoundCreate/SynchPlayback` and `DirectSoundDoWork()` inline when
  audio sync is due (`*(a7+176) != 0` path). **No semaphore/event wait was
  found in the decode path read.** The inter-frame "wait" is therefore:
  (a) the overlapped disc read completion above, and (b) the caller
  re-invoking get-frame next frame — if the caller stops invoking it,
  playback stalls with no thread blocked inside XMV (**UNCONFIRMED**,
  consistent with the observed idle-loop-only busy thread).

Kernel objects named: the movie file handle (`player+64`, created by
`sub_0021BC1C`, closed by `sub_0021BBD8`), the overlapped structure at
`player+20` (passed as `a1+5` dwords to `sub_0021C045`), and the movie
lock at `player+100` (vtable +4 lock / +12 unlock in `sub_0021A5F0`).
No `KeSetEvent`/`NtWaitForSingleObject` call site was identified inside
the XMV section in the functions read — mark the full claim
**UNCONFIRMED** pending a section-wide import-xref scan.

## 4. Fatal disc-error screen `sub_001F7EE0@0x1F7EE0`

`__noreturn`. Behaviour (full body read):

```c
byte_3B81E0 = 1;                       // disables task dispatch (§1)
sub_0021CDB0(-2, 15);
if (n3 == 0) { switch (sub_00261BEB()) { case 3: n3=3; case 4: n3=2;
               case 5: n3=5; case 6: n3=4; default: break; } }
switch (n3) { case 2: "Le disque utilis…" / "Il est peut-…"; case 3: German; …
              default: "There's a problem with the disc you're using." /
                       "It may be dirty or damaged." }
v7[0]=640; v7[1]=480; v7[2]=30; v7[4]=17; v7[5]=3; …
D3DDevice_Swap(0);
sub_00271570(v7);                      // D3DDevice_Reset(640x480) — guest addr
D3DDevice_Swap(0);
BackBuffer2 = D3DDevice_GetBackBuffer2(0);
D3DDevice_Clear(0,0,240,-12631707,1.0,0);
sub_00261DCD(&v5); sub_00261DE3(v5,32); sub_00261C74(v5,6); …
sub_00261F56(v5, BackBuffer2, Le_disque…, v2, 321,201);   // text ×3 offsets (shadow)
sub_00261F56(v5, BackBuffer2, Il_est…,   v6, 321,235);   // text ×3 offsets
D3DDevice_Swap(0);
sub_00271290();
while (1) ;                            // spin forever, never re-presents
```

So once entered it presents **exactly one** more frame (the two swaps
before Reset are the "prefix" status.md saw) and then spins in
`while(1);` — it does **not** loop presenting. Note the language selector:
`n3` is a global cached after first call; the `sub_00261BEB()` mapping
(3→3, 4→2, 5→5, 6→4) is **UNCONFIRMED** as locale IDs.

What triggers it: it has **no condition of its own** — every caller checks.
All 10 call sites:

| # | Caller | Condition at call site |
|---|---|---|
| 1 | `sub_001FF630@0x1FF87C` (CALL) | `sub_0021A5F0()==0` — XMV get-frame failed (see §3) |
| 2 | `sub_001E8600@0x1E8846` (JUMP, tail) | `sub_001E8FC0() < 0` — loading `d:\sims2.bin` / `IDirectSound_DownloadEffectsImage` failed (audio effects image, `0x1E8FC0` body read) |
| 3 | `sub_001F7A80@0x1F7B10` (CALL) | texture/surface create vtable call `(**v6)(…) == 0` after `sub_001F7A80` allocates a resource at `dword_3B8124+128/+84` |
| 4 | `sub_00211C50@0x211FC6` (CALL) | **UNCONFIRMED** — inside the 0x211C50 stream-slot allocator (tail not fully read); follows DirectSound stream bookkeeping at `dword_3EE620…` |
| 5 | `sub_002127B0@0x212829` (JUMP) | audio stream data read failed: `sub_0021B72F()==0` with `GetLastError != 997`, or overlapped `sub_0021C045(…,1)==0`; logs `"failed starting/reading audio stream data hr:: %x"` first |
| 6 | `sub_002127B0@0x212875` (JUMP) | same function, second failure branch (same check, later stage) |
| 7 | `sub_0021AB10@0x21AB1A` (CALL) | unconditional after `sub_00260F66(a1)` — `sub_0021AB10` is itself a fatal trampoline (`__noreturn`) |
| 8 | `sub_0021ABB0@0x21AC09` (CALL) | file seek failed: `sub_0021B976(…) == -1 && errno(…) == 68` (device not ready); logs `"failed seeking on file %s…"` |
| 9 | `sub_0021AC30@0x21AC77` (JUMP) | file tell failed: same `-1 && errno == 68` check; logs `"failed telling file %s"` |
| 10 | `sub_0021ACB0@0x21ADF6` (CALL) | file open failed: handle `-1` (or `byte_3B812C`) with `errno == 68`; logs `"failed opening file %s"`; skipped entirely if `n2 == 2` |

Common thread: **disc/file I/O failure with device-not-ready semantics**,
plus the movie get-frame and the audio-stream readers. It is the game's
generic "disc error" fatal, not a movie-specific error.

## 5. Ranked hypotheses for "4 frames, then silence"

Observed (status.md): `CreateDevice → Swap 1,2 → Swap 3 → Reset → Swap 4`,
one `DrawVerticesUP(8,4,…,24)` movie quad, then no draws/swaps/file opens
for 20–60 s; pad polls answered; one busy thread in idle loop
`sub_001DABD0`.

### H1 — Fatal screen entered via movie get-frame failure (most likely)
Evidence for: `sub_001FF630` calls `sub_001F7EE0()` on the *first* failed
get-frame with no retry; the fatal's `Swap → Reset(640×480) → Swap` exactly
matches swaps 3–4 + the Reset in the log (the Reset params the host logged
— 640×480, buffers 1 — match the fatal's stack block `v7`); after its final
Swap it spins in `while(1);`, matching total silence; it sets
`byte_3B81E0=1`, which freezes the task pump — matching "pad polls answered
(host-side) but nothing changes". The single successful quad draw is the
one frame decoded before the second get-frame hit a pending/failed
overlapped read (`sub_0021C045` returning 0 on `STATUS_PENDING` when not
waiting — §3).
Evidence against: the fatal draws text via `sub_00261F56` into the back
buffer before its last Swap — if the host renderer had logged those, we'd
expect more than silence afterwards, but text goes through the guest font
path (`sub_00261DCD` etc.), which may itself no-op on the host
(**UNCONFIRMED** whether those calls reach D3D).
Cheapest discriminator: **log on entry to `sub_001F7EE0`** (host hook at
guest `0x1F7EE0`). One hit refutes/confirms outright. Backup: print guest
byte `byte_3B81E0` (`0x3B81E0`) after stall — `1` means fatal (or any
setter — only writer found is the fatal).

### H2 — Decoder never yields a second frame, caller stops asking
Evidence for: get-frame returns status `3` (no frame) via `*a3` when
`*(dec+108)==0 || *(dec+96)!=0` or when the read callback returns no data
(`v11==0`); if `sub_0021A5F0`'s `>=0` test treats status-3 as success
(return path at `0x2B0171` **not read**), the quad would simply not be
redrawn and no fatal would fire — silence without any blocked thread,
matching the idle-loop sample.
Evidence against: the observed Reset between swaps 3–4 needs an explainer;
the normal path's Reset (task state 6) requires a mode-change task with
`*(task+4)∈{1,2}`, which the movie path is not known to enqueue
(**UNCONFIRMED**).
Cheapest discriminator: **log entry+return of `sub_002AFF8D` (0x2AFF8D)
with `*a3` out-value** for the first ~10 calls. Repeated `*a3==3` with
non-negative return = H2; a negative return followed by H1's hook = H1.

### H3 — Front-end sequencer waiting for a movie-finished/skip event that never fires
Evidence for: movie state `player+16` is set `2→1` on open and no other
writer was found; if the (unlocated, §1.1) sequencer polls a different
field/callback for "done", a host-side stall in e.g. the DirectSound
stream position (decode calls `IDirectSound_SynchPlayback`/`DirectSoundDoWork`
inline) could freeze advancement without any XMV thread blocking.
Evidence against: no such poll loop was identified; the present path is
called via vtable from the renderer, so a stalled sequencer would more
likely stop *presents* (swaps) but the render pump `sub_001FC040` would
still dispatch clear/present tasks — yet no swaps occur. That points back
to the pump being frozen (`byte_3B81E0`, H1) or the pump's queue being
empty because its producer died (**UNCONFIRMED**).
Cheapest discriminator: **backtrace/log the enqueue side** — log
`sub_001FC0E0` dispatches (task opcode `*task`) for 2 s after Swap 4.
Continuing opcodes with no type-4 = sequencer/producer issue (H3);
no dispatches at all = pump frozen (H1).

### H4 — Overlapped disc read never completes on the host (I/O layer)
Evidence for: the read callback treats `STATUS_PENDING (259)` as failure
when not waiting (`sub_0021C000(996)` path); the host file layer backing
`assets/` may never signal completion the way the guest overlapped code
expects, so the *second* read fails instantly → this is the mechanism
*underneath* H1 rather than an alternative. The first frame decoded
because the first read was satisfied synchronously from the header/prime
buffer (`sub_00219C50` priming in open).
Evidence against: none directly; it predicts H1's hook fires.
Cheapest discriminator: **log `sub_0021C045` (0x21C045) returns and
`*a2`** around the stall: `*a2==259` + return 0 = pending-read failure.

### H5 — Audio stream read failure fatal (sub_002127B0) instead of movie
Evidence for: same fatal, same log prefix; audio stream reads have the
identical pending-error handling (997/996).
Evidence against: the last guest activity observed was the movie quad
draw, and `sub_002127B0` logs `"failed … audio stream data …"` via
`sub_001B84E0` before jumping to the fatal — no such log was reported
(**UNCONFIRMED:** whether `sub_001B84E0` output reaches the host log).
Cheapest discriminator: same H1 hook, plus **log `sub_002127B0` entry**;
distinguishes H5 from H1 at zero extra cost if both hooks are installed.

### Recommended first instrumentation (one run)
Hook-log entries: `0x1F7EE0` (fatal), `0x2AFF8D` (get-frame, with `*a3`
out), `0x21C045` (overlapped poll, with `*a2`), `0x1FC0E0` (task opcode).
Print guest bytes/dwords at stall: `byte_3B81E0@0x3B81E0`,
`player+16` (movie state — player address from `sub_00219EE0`'s `a1`,
capture at open), settings byte at `settings+0x381` (capture the settings
object at `sub_0002A300` entry). That single run separates H1/H2/H3/H5
and, if H1+H4 confirm, the fix belongs in the host file/overlapped layer
(or the guest-byte `skip_startup_movies` write of §2 to route around XMV
entirely) — not in the renderer.
