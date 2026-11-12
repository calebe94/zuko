# Plan: rt A/B Bypass + WAV Recording

> Status: PLAN ONLY — no source files modified.

---

## 1. Goal / Non-goals

**Goals**
- Single-keypress (`b`) A/B bypass: routes input → output through DSP or dry path, toggled live during playback with no stream restart.
- Single-keypress (`r`) recording toggle: writes two timestamped mono 16-bit PCM WAV files (dry + wet) per session, 48 kHz.
- Both features work in duplex mode AND split-clock (two-stream) mode.
- Terminal restored cleanly on Enter, Ctrl+C, or crash.

**Non-goals**
- Multi-channel or stereo recording.
- Real-time DSP parameter editing (separate concern).
- Stereo WAV (dry is always mono source; wet is mono post-DSP).
- Any external library dependency (no ffmpeg, no libFLAC, no libsndfile).
- Sample-exact latency matching between dry and wet (they will differ by ~`kCabFirTaps/2` samples due to FIR group delay; this is expected and documented).

---

## 2. Architecture

### 2.1 New global state (all in `rt.cpp` anonymous namespace)

```cpp
// Bypass
std::atomic<bool> g_bypass{false};          // relaxed load/store in callback

// Recording
std::atomic<bool> g_recording{false};       // toggled by keypress
Ring g_dry_ring;                            // pre-DSP samples → writer
Ring g_wet_ring;                            // post-DSP (bypass-aware) → writer

// Writer thread lifecycle
std::atomic<bool> g_writer_active{false};   // set true when recording starts
std::thread       g_writer_thread;

// Terminal
struct termios    g_saved_termios;
std::atomic<bool> g_quit{false};
bool              g_tty_ok = false;  // false when stdin is not a TTY
```

New headers required (review nit #9): `<thread>`, `<chrono>`, `<signal.h>`
(sigaction), `<termios.h>`, `<unistd.h>` (isatty/read), `<sys/stat.h>` (mkdir),
`<ctime>` (timestamps), `<algorithm>` (std::clamp/min), `<cerrno>` (EINTR) —
added in Task 1 (termios/signal) and Task 4 (thread/chrono/algorithm).

`Ring` (line 64–82) is reused unchanged — `g_dry_ring` and `g_wet_ring` are additional instances. No new lock types introduced.

### 2.2 Capture points

#### Split-clock mode — two streams

Current `input_callback` (line 86–92) calls `g_engine.process(in[i])` and pushes to `g_ring`. Modified:

```
input_callback(in[], n):
  for i in 0..n-1:
    dry = in[i]                                      // capture point: DRY
    wet = g_bypass ? dry : g_engine.process(dry)     // bypass check
    g_ring.push(wet)                                 // existing path, unmodified
    if g_recording:
      g_dry_ring.push(dry)
      g_wet_ring.push(wet)
```

`output_callback` (line 94–103) is **unchanged** — it only reads `g_ring`, which already carries the bypass-aware signal. No recording capture needed there; both dry and wet are already captured per-sample in `input_callback` with zero skew.

#### Duplex mode — single stream

Current `duplex_callback` (line 105–115) calls `g_engine.process(in[i])` directly. Modified:

```
duplex_callback(in[], out[], n):
  for i in 0..n-1:
    dry = in[i]                                      // capture point: DRY
    wet = g_bypass ? dry : g_engine.process(dry)     // bypass check
    out[2*i] = out[2*i+1] = wet                      // existing path
    if g_recording:
      g_dry_ring.push(dry)
      g_wet_ring.push(wet)
```

### 2.3 Data-flow diagram

```
                    ┌──────────────────────────────────────────────┐
                    │           AUDIO CALLBACK (RT, no I/O)        │
                    │                                               │
  USB in ──────────►│ dry = in[i]                                   │
                    │   │                                           │
                    │   ├─── g_bypass? ──────────────────────────► wet
                    │   │        │no                               │
                    │   │   g_engine.process(dry) ───────────────► wet
                    │   │                                           │
                    │   │  if g_recording:                          │
                    │   ├──────────────────► g_dry_ring.push(dry)  │
                    │   └──────────────────► g_wet_ring.push(wet)  │
                    │                                               │
                    │  [split-clock only]                           │
                    │  g_ring.push(wet) ──► output_callback        │
                    │  [duplex]                                     │
                    │  out[2i] = out[2i+1] = wet                   │
                    └──────────────────────────────────────────────┘
                              │                    │
                         g_dry_ring           g_wet_ring
                              │                    │
                    ┌──────────────────────────────────────────────┐
                    │      WRITER THREAD (background, non-RT)      │
                    │                                              │
                    │  loop (sleep 1 ms):                          │
                    │    n = min(dry_ring.avail, wet_ring.avail)   │
                    │    for 0..n-1:                               │
                    │      dry_file << dry_ring.pop()              │
                    │      wet_file << wet_ring.pop()              │
                    │    (same popped count → files stay aligned)  │
                    │    if !g_recording && both rings empty:      │
                    │      patch RIFF sizes → close files          │
                    └──────────────────────────────────────────────┘
```

**Ring policy note (review finding #2):** the existing `Ring` drop-oldest policy
is NOT safe for the record rings — `push()` mutates `read_` on overflow
(rt.cpp:71) while the writer thread concurrently mutates it in `pop()`, which is
a data race, and a desync between the two rings would misalign dry/wet.
Mitigation: the record rings are sized for SECONDS of backlog (not 85 ms) so
overflow is a hard cap that never triggers in practice, AND the writer drains
both rings in lockstep (same popped count per cycle) so the files can never
diverge. Overflow is logged once when it happens.

### 2.4 Writer thread design

One thread (`g_writer_thread`) is started when recording begins and joins when recording ends. This is simpler than a persistent thread because:
- No need for condition variables or semaphores.
- Thread state is fully encapsulated in its stack/closure.
- Recording sessions are infrequent (seconds to minutes).

Thread lifecycle:
1. `start_recording()` called from main thread (keypress 'r'): opens files, writes placeholder RIFF headers, starts thread.
2. Thread spins: drains both rings, converts `float → int16_t` (clamp + scale), `fwrite` to files. Sleeps 1 ms when both rings are empty.
3. `stop_recording()` called from main thread: sets `g_recording = false`, joins thread. Thread drains remaining samples, patches header sizes, closes files.
4. `join()` guarantees all samples are flushed before `stop_recording()` returns.

**Open-failure guard (review finding #3):** `start_recording()` must abort
(leave `g_recording = false`, do NOT launch the writer thread) if `mkdir`
fails for a reason other than EEXIST or if either `fopen` returns NULL.
Otherwise the writer thread would `fwrite` to a NULL FILE* and crash with the
terminal still in raw mode.

**Recovery patching (review finding #7):** the writer thread re-patches the
RIFF sizes (fseek + rewrite fields 4 and 40) at every cycle, so a recording
killed abnormally (SIGKILL, crash) still leaves a valid, playable file up to
the last flushed block. Files are opened `"wb+"` to allow the periodic
seek-and-patch.

Ring sizing: the record rings hold several SECONDS of backlog (see the ring
policy note after the data-flow diagram). Combined with the lockstep drain, the
drop-oldest cap is not expected to trigger; if it ever does, it is logged once
at stop.

### 2.5 WAV header (self-written, no library)

44-byte RIFF/PCM header (same layout as `harness.cpp:write_wav`, line 85–108):

| Offset | Field             | Value at open         | Patched at close |
|--------|-------------------|-----------------------|-----------------|
| 0      | `RIFF`            | "RIFF"                | —               |
| 4      | riff_size         | 0 (placeholder)       | data_len + 36   |
| 8      | `WAVE`            | "WAVE"                | —               |
| 12     | `fmt `            | "fmt "                | —               |
| 16     | fmt_chunk_size    | 16                    | —               |
| 20     | audio_format      | 1 (PCM)               | —               |
| 22     | channels          | 1                     | —               |
| 24     | sample_rate       | 48000                 | —               |
| 28     | byte_rate         | 96000 (48000×1×2)     | —               |
| 32     | block_align       | 2 (1 ch × 2 bytes)    | —               |
| 34     | bits_per_sample   | 16                    | —               |
| 36     | `data`            | "data"                | —               |
| 40     | data_size         | 0 (placeholder)       | n_samples × 2   |

On close: `fseek(f, 4, SEEK_SET)` → write riff_size; `fseek(f, 40, SEEK_SET)` → write data_size.

### 2.6 Terminal input

Replace `getchar()` at line 273 with a termios-based read loop in `main()`. No extra thread needed.

```cpp
// Setup (once, before audio streams start):
// Non-TTY guard (review finding #6): if stdin is not a TTY (piped input,
// CI), skip raw mode and key handling entirely; g_tty_ok gates both.
g_tty_ok = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_saved_termios) == 0;
if (g_tty_ok) {
    struct termios raw = g_saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO);   // non-canonical, no echo
    raw.c_lflag |= ISIG;                // keep ISIG: Ctrl+C still sends SIGINT
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

// Restore function (registered with atexit + SIGINT handler):
void restore_terminal() { tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios); }

// SIGINT handler:
void sigint_handler(int) {
    g_quit.store(true, std::memory_order_relaxed);
    // atexit handler will restore termios
}
// macOS/BSD signal() sets SA_RESTART, which auto-restarts the blocked read()
// so EINTR is NEVER delivered. Use sigaction WITHOUT SA_RESTART so Ctrl+C
// interrupts the read and the loop sees g_quit. (Review finding #1.)
struct sigaction sa{};
sa.sa_handler = sigint_handler;   // sets g_quit (relaxed), async-signal-safe
sa.sa_flags   = 0;                // NO SA_RESTART
sigemptyset(&sa.sa_mask);
sigaction(SIGINT, &sa, nullptr);
atexit(restore_terminal);

// Main read loop (replaces getchar() at line 273):
if (g_tty_ok) {
    char ch;
    while (!g_quit.load() && read(STDIN_FILENO, &ch, 1) == 1) {
        if      (ch == 'b')  toggle_bypass();
        else if (ch == 'r')  toggle_recording();
        else if (ch == '\r' || ch == '\n') break;
        // ch == 3 (^C) is not reached because ISIG delivers SIGINT instead
    }
} else {
    // No TTY: no key handling (DECIDED — review finding #6). Block on EOF
    // from stdin; signal handling still stops the process cleanly.
    while (!g_quit.load() && getchar() != EOF) {}
}
// With sigaction (no SA_RESTART), Ctrl+C makes read() return -1/EINTR, the
// loop condition goes false, and normal cleanup + atexit restore run.
```

PortAudio on macOS (CoreAudio backend) does **not** install its own SIGINT handler. Our handler fires first and cleanly. After the loop, existing cleanup code (line 274–278) runs unchanged.

**atexit + SIGINT ordering**: `atexit` handlers run on `exit()` but NOT on signal default action (which calls `_exit`). With our custom `sigint_handler` that sets `g_quit`, the main loop exits normally, calls existing cleanup, then `return 0` → `exit()` → atexit fires. Terminal is always restored.

### 2.7 recordings/ directory

Created relative to cwd. At runtime the Makefile `run` target executes `cd $(REPO)/host && ./rt/rt ...`, so recordings appear in `host/recordings/`. Standalone invocations (`./rt/rt` from `host/rt/`) produce `host/rt/recordings/`. This is acceptable and matches user expectations (recordings near the binary).

Creation: `mkdir("recordings", 0755)` — POSIX, no dependency. Ignore EEXIST.

---

## 3. Task breakdown

### Task 1 — termios keypress loop (S)
**What**: Replace `getchar()` (line 273) with termios non-canonical setup, SIGINT handler, atexit restorer, and char-dispatch loop. Add stubs: `toggle_bypass()` prints "BYPASS ON" / "BYPASS OFF" and returns. `toggle_recording()` prints "REC START" / "REC STOP" and returns.  
**Files**: `host/rt/rt.cpp` only.  
**Acceptance test**: `make -C host/rt && ./host/rt/rt --list` (no audio needed). Launch with any preset, press 'b' → prints "BYPASS ON (dry path)". Press 'b' again → "BYPASS OFF (DSP path)". Press Enter → exits cleanly. Terminal prompt is usable afterwards (no stuck raw mode). `make -C host test` passes.

### Task 2 — bypass flag in callbacks (S)
**What**: Add `std::atomic<bool> g_bypass{false}`. In `input_callback` (line 90): read `g_bypass`, conditionally skip `g_engine.process()`. In `duplex_callback` (line 110): same. `toggle_bypass()` atomically flips flag and prints status.  
**Files**: `host/rt/rt.cpp` only.  
**Acceptance test**: Live audio test. Enable bypass → guitar sounds dry (no cab coloring). Disable bypass → cab sim audible. `make -C host test` passes.

### Task 3 — recording rings (S)
**What**: Add `Ring g_dry_ring, g_wet_ring` alongside existing `g_ring` (line 84). Add `std::atomic<bool> g_recording{false}`. In `input_callback` (line 90) and `duplex_callback` (line 110): after the bypass-aware computation, `if (g_recording.load(relaxed)) { g_dry_ring.push(dry); g_wet_ring.push(wet); }`.  
**Files**: `host/rt/rt.cpp` only.  
**Acceptance test**: Compile only (no observable side effect yet). `make -C host/rt` clean build. `make -C host test` passes.

### Task 4 — WAV writer (M)
**What**: Implement `WavWriter` struct with `open(path, sr)` / `write(float)` / `close()`. Self-contained RIFF header using `<cstdio>` only (no new headers). Files opened `"wb+"` for the periodic size re-patch. Conversion: hard clip `int16_t s = int16_t(std::clamp(x, -1.f, 1.f) * 32767.f)` with a clipped-sample counter printed once at `close()` (DECIDED — no soft limiter; these are diagnostic recordings). On `close()`: seek back and patch sizes.
Also implement `start_recording()` / `stop_recording()`: open two WavWriter instances, start/join writer thread. `start_recording()` aborts with an error line if `mkdir` fails (≠ EEXIST) or either `fopen` returns NULL (review finding #3). Writer thread loop: lockstep drain (same popped count from both rings) every 1 ms sleep, re-patching RIFF sizes each cycle (review finding #7).
**Files**: `host/rt/rt.cpp` only (add WavWriter struct in anonymous namespace; keep it under ~80 lines).
**Acceptance test**: `make -C host/rt`. MANUAL ONLY for live audio (guitar strum; forbidden in CI/agent sessions): press 'r', play 3 s, press 'r'. Objective fallback for automated verification: record with no input device activity (silence) and validate the files — `python3 -c "import wave; w=wave.open('...'); print(w.getnframes(), w.getframerate())"` shows ~144000 frames at 48000 Hz, 1 channel, 16-bit, plus the header-validity script from §4b. `make -C host test` passes.

### Task 5 — alignment + toggle_recording wiring (S)
**What**: Wire `toggle_recording()` to call `start_recording()` / `stop_recording()`. Print timestamped status: `"REC START → recordings/zuko-rt_20261102-153000-042_dry.wav"` — filenames carry a millisecond field plus a same-second session counter (`...-HHMMSS-mmm` with an NNN counter suffix) so two sessions in the same second can never overwrite each other (review finding #5). Add graceful stop: if `g_recording` is true when the main loop exits (Enter or Ctrl+C), call `stop_recording()` before stream teardown.
**Invariant (review nit #10)**: `stop_recording()` ALWAYS joins the writer thread before returning, and `start_recording()` refuses to launch if a thread is still joinable. The single-threaded keypress loop serializes toggles, so rapid 'r' is ordered start→stop→start, never concurrent.
**Files**: `host/rt/rt.cpp` only.
**Acceptance test**: Press Enter while recording → recording stops cleanly before streams close. Files complete (no truncated RIFF). `make -C host test` passes. (Live-recording behaviour itself is MANUAL-ONLY — see Task 4 note.)

---

## 4. Verification plan

### (a) Bypass latency equivalence
Bypass and DSP path must have identical **hardware** latency (same PortAudio stream, same buffer size). There is no extra software latency because the bypass toggle only changes one branch in the callback — no stream restart, no buffer size change. This concerns STEADY-STATE latency only (review finding #4): in split-clock mode the output drains the ~85 ms `g_ring` backlog, so the audible A/B switch lags the keypress by the current ring fill, and the dry↔wet transition is a hard step (audible click, no crossfade). Both are expected and accepted for diagnostic A/B; documented here and in the usage line.

Test: Record a sharp transient (pluck or click) with bypass ON; record again with bypass OFF. Verify the peak onset sample in `zuko-rt_*_dry.wav` (dry path) and the peak in `zuko-rt_*_wet.wav` (DSP path) differ by no more than the cab FIR group delay (expected: ~`kCabFirTaps/2` samples, since the cab FIR is linear-phase). Command:

```bash
python3 - <<'EOF'
import wave, numpy as np
def peak(f):
    w = wave.open(f); d = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16); return np.argmax(np.abs(d))
dry = peak("recordings/zuko-rt_..._dry.wav")
wet = peak("recordings/zuko-rt_..._wet.wav")
print(f"dry peak @ {dry}, wet peak @ {wet}, delta = {wet - dry} samples")
EOF
```

Expected: delta ≈ `kCabFirTaps/2` for DSP path; delta ≈ 0 for bypass.

### (b) WAV header validity
```bash
python3 -c "
import struct, sys
f = open(sys.argv[1], 'rb')
h = f.read(44)
riff_size  = struct.unpack_from('<I', h, 4)[0]
data_size  = struct.unpack_from('<I', h, 40)[0]
file_size  = __import__('os').path.getsize(sys.argv[1])
assert h[:4] == b'RIFF' and h[8:12] == b'WAVE'
assert riff_size == file_size - 8, f'{riff_size} != {file_size-8}'
assert data_size == file_size - 44, f'{data_size} != {file_size-44}'
n_samples  = data_size // 2
print(f'OK: {n_samples} samples = {n_samples/48000:.2f}s')
" recordings/zuko-rt_..._dry.wav
```

### (c) Dry/wet sample alignment
Both are captured from the same loop iteration in `input_callback` / `duplex_callback`, so they are written in lockstep. Verify equal frame counts:

```bash
python3 -c "
import wave
d = wave.open('recordings/zuko-rt_..._dry.wav')
w = wave.open('recordings/zuko-rt_..._wet.wav')
assert d.getnframes() == w.getnframes(), 'frame count mismatch'
print(f'aligned: {d.getnframes()} frames each')
"
```

### (d) Clean stop / no truncation
1. Start recording, immediately kill with `kill -9 <pid>` → redo with Ctrl+C (SIGINT) instead. SIGINT path: sigint_handler sets `g_quit`, main loop exits, `stop_recording()` called before `Pa_Terminate()`, files closed cleanly. Run header validity check (b) on resulting files.
2. `make -C host test` passes after feature is complete.

---

## 5. Risks + mitigations

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| **Writer thread falls behind disk I/O** | Low — 1 ms sleep drains ~48 samples/ms; modern SSD writes GB/s. Only a risk on a heavily swapped machine. | Ring overflow drops oldest, same as `g_ring`. Log overflow count on stop (count is non-RT: check after join). |
| **Raw terminal left broken after SIGKILL or crash** | Medium | `atexit(restore_terminal)` covers `exit()`. SIGKILL cannot be caught — user recovers with `reset` or `stty sane`. Document in usage output. |
| **Raw terminal left broken after SIGSEGV / other crash** | Low | Add `signal(SIGSEGV, sigint_handler)` as extra guard if desired. Not required for MVP. |
| **Disk full during recording** | Low | `fwrite` returns short count. Check return value in writer; set an error flag, stop recording, print warning. Files remain seekable for size-patch but data is truncated. |
| **Ring overflow during recording** | Very low | 85 ms buffer vs ~1 ms drain cycle. If it occurs, oldest samples are lost. Same trade-off already accepted in `g_ring`. |
| **PortAudio SIGINT conflict** | Very low — CoreAudio backend does not install SIGINT handler. | Verified by inspection. If a future backend differs, our `signal()` call must come AFTER `Pa_Initialize()` to override. |

---

## 6. Open questions — RESOLVED (decisions applied to the plan)

1. **recordings/ location**: DECIDED — `cwd/recordings/`, no flag. Simplest correct behaviour; documented in the usage line.
2. **Clipping on WAV output**: DECIDED — hard clip with a clipped-sample counter printed once at close. Diagnostic recordings, not mastering.
3. **Recording when bypass is ON**: DECIDED — intended. The wet WAV records exactly what the listener hears; in bypass both files are identical, which is itself useful A/B evidence.
4. **stdin not a TTY** (added by review, finding #6): DECIDED — `isatty()` guard; without a TTY the raw mode and key handling are skipped, and the process blocks on stdin EOF, still stoppable by signals.

---

## Appendix: Sources read

| File | Lines read | Purpose |
|------|-----------|---------|
| `host/rt/rt.cpp` | 1–279 (full file) | Understand callback structure, ring, getchar, stream modes |
| `host/rt/Makefile` | 1–30 (full file) | Build system, cwd for `make run`, no new deps constraint |
| `host/harness.cpp` | grep for WAV/RIFF patterns | Confirm existing WAV read/write pattern for RIFF header layout |
| `host/ini_map.h` | (not read in full — not needed for this plan) | — |
| `docs/` listing | directory listing | Confirmed `docs/` exists; no existing plan-rt file |
| `host/` listing | directory listing | Confirmed no existing `recordings/` dir |

**Could NOT do**: Live audio testing (not permitted in this session).

**Post-review status**: reviewed by Claude Opus 4.8 (high effort) — verdict
APPROVED WITH CHANGES; all 10 findings are incorporated above. The FIR group
delay assumption is now CLOSED: `kCabFirTaps = 512` (src/dsp/cab_ir.h:9), the
IR is a symmetric windowed-sinc lowpass (linear phase), so group delay ≈ 256
samples ≈ 5.3 ms at 48 kHz; tone-stack biquads add a small extra phase, so the
measured dry/wet peak delta may be slightly above 256 samples.
