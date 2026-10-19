# Audio — The Sims 2 (USA), XDK 5849: verification record

## Verdict

Audio works. The original "no audio at all" report did not reproduce once
the game was run **unmuted**: every earlier bring-up run used `XBE_MUTE=1`,
which zeroes the mastering voice (`sdk/runtime/audio/xaudio2.cpp`), so there
was never any evidence of a guest-side silence defect. The owner confirmed
audible sound on an unmuted run on 2026-10-18. This document records the
host-side proof, per audio path, from the instrumented build in
`out/build/audio` (RelWithDebInfo, verbose log, no `XBE_MUTE`).

Original four-case question — (a) nothing reaches XAudio2, (b) voices but
no buffers, (c) buffers of silence, (d) real data but wrong output — the
answer is **none of them**: real, non-silent data reaches XAudio2 on every
path exercised (below). Case (d)'s mute sub-case was the entire story, and
it was the harness flag, not the port.

## Instrumentation (kept, log level `LOGI`)

Host-side counters only; no guest behaviour changes. In release
(`NDEBUG`) builds none of this prints.

- `sdk/runtime/audio/xaudio2.cpp`: mastering/source voice creation line
  (`audio: mastering voice ok, source voice 48000 Hz 2 ch IEEE float`);
  every 500 submitted 10 ms buffers: submitted/rendered counts, XAudio2
  queue depth, and the mixed output's peak amplitude since the last line.
- `sdk/runtime/xdk/dsound.cpp`: per-format line (first 64:
  `dsound: format tag=%X ch=%u rate=%u align=%u bits=%u`), buffer creation
  (first 48) and stream creation lines with tag/channels/rate, packet
  submissions (first 24 `CDirectSoundStream_Process` calls), and every 500
  mixer renders (~5 s) a census: live voices, sounding voices, queued
  packets/bytes, cumulative `Process` calls/bytes, packet completions,
  buffer `Play`/`PlayEx`, `SetBufferData`, stream `GetStatus` counts, and
  the mixer's peak amplitude since the last line.

A temporary `SIMS2_AUDIO_WAV` mixed-output capture was used for the
per-second peak/RMS table below and then removed.

## Evidence — run `out/build/audio/logs/sims2_002.log` (110 s, `SIMS2_AUTOSTART=1`, unmuted)

### Boot / bank load (t = 0.36 s)

- `open D:\data\audiostr.xwb`, then 48 `IDirectSoundBuffer` voices created:
  `tag=1 ch=1 rate=44100 bits=16 align=2` (PCM, mono, 44.1 kHz), each
  `bytes=0` at creation (data is supplied later via `SetBufferData`).
- `dsound: effects image from memory, 1 effects` — the effects image is
  accepted; the host keeps its descriptor with zeroed DSP segments. No
  voice depends on it for audibility: every sounding voice below peaks
  well above zero with default/front mix bins.

### Intro movies — `ea_games.xmv`, `s2c_intro.xmv` (different path: XMV → ADPCM stream)

- 0.39 s: `stream #1 tag=69 ch=2 rate=44100 align=72 maxpk=2 cb=002AFD37`
  for `ea_games.xmv`; 10.56 s: `stream #2 tag=69 ch=2 rate=48000` for
  `s2c_intro.xmv`. **Format tag 0x69 is Xbox ADPCM**; the host decodes it
  (`DecodeBlock` in `dsound.cpp`, 36 bytes/channel → 64 frames). The
  `dsound: format tag %X not supported` path was **never hit** in any run
  (formats seen: `1` and `0x69` only).
- Movie 1: 12 packets, 150,552 bytes submitted and 12 completed by 5.3 s;
  mixer/XAudio2 peak **0.3548**. Movie 2: 25 packets, 268,704 bytes by
  15.3 s; peak **0.4962**.
- Mixed-output WAV (first 20 s), per-second peak/RMS: s0 0.355/0.076,
  s1 0.219/0.050, s2 0.046/0.013, s3 0.022/0.001, s4–s9 silent (the EA
  sting is ~3 s; gap between movies), s10 0.338/0.056, s11 0.496/0.074,
  s12 0.285/0.028, s13–s19 silent (intro skipped at ~22 s in run 1;
  in run 2 the second movie's submitted audio also ends ~13 s).
  Silence between movies is therefore content, not a defect: packet
  submission and completion counts stop exactly when the peak does.

### Title screen and menu (game's own streaming channel)

- 22.12 s (Start press skips the intro): `SAMPLES.ARC`/`AUDIOSTR.ARC`
  open; `stream #3 tag=69 ch=2 rate=48000 align=72 maxpk=5 cb=00000000` —
  the game's music channel (host `DirectSoundCreateStream` fed by the
  guest stream-source object, see the standing note). From then to the
  end of the 110 s run the stream never stalls:
  - `Process` bytes grow 711,072 → 5,282,208 (≈59 KB/s ≈ 48 kHz stereo
    ADPCM rate), completions track submissions (57 of 59 at 110 s; the
    2-packet difference is the guest source's double buffer),
  - queued bytes hold at 294,912 (2 × 147,456-byte packets),
  - `sounding=2` throughout, census peak **0.25–0.59 in every 5 s
    window**, XAudio2 `submitted` reaches 11,000 buffers with queue
    depth 2 (no underrun, no backlog growth).
- One further stream in run 1 at 50.7 s: `stream #4 tag=69 ch=1
  rate=24000 align=36` (mono 24 kHz ADPCM — an ambient/effects stream),
  with `sounding` rising to 4.
- PCM SFX buffers: `plays=0` (Play/PlayEx) and `setdata=0` for the whole
  autostart session — the scripted pad's menu navigation never triggered
  a buffer-play in these runs, so the one-shot SFX path is **created and
  correctly formatted but not yet observed sounding**. This is a coverage
  gap, not a silence proof: the same buffers are the ones gameplay
  drives (next section).

### Gameplay on a lot

PENDING final verification run (machine window). Markers to capture:
`SOUNDEVE.ARC` opening, new stream/buffer activity, `plays`/`setdata`
rising above zero, and census/XAudio2 peaks > 0 while on the lot.

## Standing note — five `.XTLID` exports still run guest console code

Boot logs `dsound: exports running their original code: B00A3 at
002A5A36 B00A5 at 002A5A80 B00A4 at 002A5A85 B0080 at 002A69AC B00A9 at
002A69ED`. Full reverse engineering: `docs/analysis-dsound-xtlid.md`.
Disposition now that audio demonstrably works:

- `B00A5`/`B00A4` (WAVEFORMATEX builders, PCM tag 1 and ADPCM tag 0x69)
  run as guest code on every stream/buffer setup above and produce
  exactly the formats the host consumes — the logged tags/rates are
  their output. Benign as-is; replacing them risks changing the format
  contract for zero benefit.
- `B00A3` (Hz→pitch converter) is reachable only from guest inners the
  host already replaces. Benign.
- `B0080` (FlushEx forwarder) forwards into the host
  `CDirectSoundStream_FlushEx`. Benign.
- `B00A9` (guest stream-source create) is the one to watch: the game
  feeds the host music/effects streams *through* this guest object
  (Process / seek / status / Release). The title/menu evidence above
  shows it working — sustained 59 KB/s with completions tracking
  submissions for 88 s. The known hazard is at teardown: its guest
  destructor walks the guest CRT stream list, which is how a corrupted
  list node (0xFF000024 read) faulted in earlier analysis. That is a
  lifetime/corruption defect if it recurs, not an audio-format one.
  Do **not** host-replace `B00A9` with an inert stub: a replacement
  must honour the vtable data contract or the music path above goes
  silent. No `[symbols]` line is proposed for any of the five.

## Files changed for this verification

- `sdk/runtime/audio/xaudio2.cpp` — voice-creation log + submitted/peak
  counters (kept). Temporary WAV capture added for the table above and
  removed afterwards.
- `sdk/runtime/xdk/dsound.cpp` — format/creation/process logs + mixer
  census counters (kept; all `LOGI`/`LOG_FIRST`, silent in release).
- No behavioural fix was needed: no format check, guard, or volume was
  touched.
