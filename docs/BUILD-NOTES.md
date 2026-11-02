# Build notes — verified evidence

Everything below comes from real command output on 2026-10-30. Nothing here is
an estimate or a reproduction from documentation.

Environment: Docker image built from this repository's `Dockerfile`
(`python:3.11-slim-bookworm`), host architecture `aarch64`, PlatformIO Core from
pip, platform `https://github.com/maxgerhardt/platform-raspberrypi.git`, board
`rpipico2`, core `earlephilhower`.

## Results

| # | What was run | Result | Evidence |
|---|---|---|---|
| 1 | `pio run -e rpipico2` with the brief's `-mfloat-abi=hard -mfpu=fpv5-sp-d16` | **FAIL after 165 s** | `bits/c++config.h: No such file or directory`, raised from `c++/16.1.0/cstdlib` |
| 2 | `pio run -e rpipico2` without those two flags | **SUCCESS after 143 s** | RAM 1.7% (8656 / 524288 B), flash 0.9% (37584 / 4190208 B), `firmware.uf2` = 99840 B |
| 3 | `arm-none-eabi-objdump -d firmware.elf` | **hardware FPU in use** | `vfma.f32`, `vmul.f32`, `vadd.f32`, `vdiv.f32` present in the waveshaper/FIR code; **no** `__aeabi_fmul` anywhere |
| 4 | CMSIS-DSP link probe: a copy of the project that actually calls `arm_fir_init_f32` + `arm_fir_f32` | **SUCCESS, links** | no `undefined reference`; RAM grew 1.7% -> 2.0%, flash 0.9% -> 1.0% because the library is now linked in |
| 5 | `grep -ao "arm_fir_[a-z0-9_]*" libCMSISDSP.a \| sort -u` | **full FIR API present** | includes `arm_fir_f32`, `arm_fir_init_f32`, `arm_fir_decimate_f32`, `arm_fir_interpolate_f32` |

## Finding 1 — hard-float breaks the build (documented in `platformio.ini`)

The arduino-pico core compiles RP2350 code with
`-mcpu=cortex-m33 -march=armv8-m.main+fp+dsp -mfloat-abi=softfp`, and the
toolchain PlatformIO installs for that core ships no hard-float multilib. Asking
for `-mfloat-abi=hard` makes the compiler look for the C++ headers in a multilib
subdirectory that does not exist, so the failure appears while preprocessing
`<cstdlib>` — a confusing error for a flag that has nothing to do with C++.

`softfp` changes only the calling convention, not the generated instructions: the
FPU is still used (result 3 above). The two flags are therefore removed, and this
is a deliberate, verified deviation from the original brief.

## Finding 2 — the Dockerfile installed nothing (fixed)

The generated Dockerfile used `pio pkg install -g -p <url>`. In PlatformIO Core,
`-p` is `--project-dir`, **not** `--platform`, so the image contained neither the
platform nor the toolchain: the first build silently re-downloaded everything
inside a throwaway container (which is why it took 165 s instead of ~20 s for the
compile alone). Every failure above was produced by that run.

Correct form, now used in both the `Dockerfile` and the CI workflow:

```sh
pio pkg install --global --platform https://github.com/maxgerhardt/platform-raspberrypi.git
```

## Known limitation

The image installs the PlatformIO **platform**, not the Arduino core's
**toolchain**, which PlatformIO fetches on the first build (~200 MB). The CI
workflow caches `~/.platformio`, so this only costs time on a cold cache. Baking
the toolchain into the image would mean running a warm-up build during
`docker build`; not done, because it doubles image build time for a one-off
saving.

## Reproduce

```sh
cd <repo>
docker build -t zuko-rp2350-build .

# build + FPU proof + CMSIS link proof
docker run --rm -v "$PWD":/src zuko-rp2350-build bash /src/.hermes/verify-2.sh

# the reference symbols in the CMSIS archive (the archive is left in the repo)
grep -ao "arm_fir_[a-z0-9_]*" \
  .pio/libdeps/rpipico2/ArduCMSIS-DSP/src/cortex-m33/libCMSISDSP.a | sort -u
```

The probe scripts under `.hermes/` are verification scaffolding, not deliverables.

## Phase 1 — I2S driver (PIO + DMA ping-pong)

**Build VERIFIED in Docker by the orchestrator, 2026-10-31.** The Phase 1 code was
written from documentation and framework source inspection (no local PlatformIO
toolchain, no board). As written it did **not** compile; one fix was needed and is
recorded below. The corrected tree builds. Runtime behaviour is still unverified —
there is no board.

### Verified results (real command output, not estimates)

| # | What was run | Result | Evidence |
|---|---|---|---|
| 1 | `pio run -e rpipico2` | **SUCCESS** | RAM 1.9% (9960 / 524288 B), flash 1.0% (40904 / 4190208 B), `firmware.uf2` = 106496 B |
| 2 | `pio run -e rpipico2 -t size` | **SUCCESS** | text 557028, data 12288, bss 6848, dec 576164 |
| 3 | `arm-none-eabi-objdump -d firmware.elf` | hardware FPU in use | `vfma.f32` present (2 occurrences), **no** `__aeabi_fmul` anywhere |
| 4 | `arm-none-eabi-nm --print-size firmware.elf` | block handler and ISR are in RAM | `dma_irq_handler` @ `0x20000110` (152 B), `audio_passthrough` @ `0x200001a8` (14 B) — both in the `0x2000_0000` SRAM region, so `__not_in_flash_func` took effect |
| 5 | `pioasm -o hex` on the reconstructed PIO programs | hand-assembled opcodes are correct | all 28 words match exactly (table below) |

Exact commands (inside the build image; `zuko-pio-cache` is a named volume holding
`/root/.platformio` so the Arduino core is fetched once):

```sh
docker run --rm -v zuko-pio-cache:/root/.platformio \
  -v "$PWD":/workspace -w /workspace zuko-rp2350-build pio run -e rpipico2
docker run --rm -v zuko-pio-cache:/root/.platformio \
  -v "$PWD":/workspace -w /workspace zuko-rp2350-build pio run -e rpipico2 -t size
```

### The one fix the draft needed

`src/i2s.cpp` called `rp2040.getCycleCount()` (the arduino-pico core cycle counter)
without including `Arduino.h`, which is what declares the `rp2040` object. The first
build failed with:

```
src/i2s.cpp: In function 'void dma_irq_handler()':
src/i2s.cpp:164:23: error: 'rp2040' was not declared in this scope
*** [.pio/build/rpipico2/src/i2s.cpp.o] Error 1
```

Fixed by adding `#include <Arduino.h>` to `src/i2s.cpp`. Nothing else was changed.

### PIO opcode cross-check

The three PIO programs in `src/i2s.cpp` are hand-assembled `uint16_t` arrays rather than
a `.pio` file. They were re-assembled independently from the same mnemonics with the
`pioasm` that ships with the platform
(`~/.platformio/packages/tool-pioasm-rp2040-earlephilhower/pioasm`). Every word matched:

| Program | Opcodes (both assemble identically) |
|---|---|
| `pio_mclk` | `e001 e000` |
| `pio_out_master` | `e03e 8880 6001 0842 f03e 9880 7001 1846` |
| `pio_in_slave` | `20a1 4001 8000 2021 2022 20a1 4001 2021 00ca 0005 20a1 4001 8000 2021 20a1 4001 2021 00ce` |

`pioasm` also confirms that the `public entry_point` of the input program lands at
instruction index 4, which is the value of `PIO_IN_SLAVE_ENTRY` in the driver.

### What this build does NOT prove

Compiling and linking proves that every API name, struct layout and register alias used
here is real, and that the code fits the image. It does **not** prove I2S timing, that the
DMA ping-pong actually alternates buffers, or that audio survives the round trip. Those
are the hardware acceptance tests listed in `docs/I2S.md`, and they need a board.

### Files added or modified

| File | Action |
|---|---|
| `src/i2s.h` | new — public I2S driver interface |
| `src/i2s.cpp` | new — PIO programs (hand-assembled), DMA ping-pong engine |
| `src/main.cpp` | modified — Phase 1 banner, audio pass-through callback, budget report |
| `docs/I2S.md` | new — design as built |
| `docs/BUILD-NOTES.md` | appended — this section |
| `README.md` | updated — status and layout |

### APIs used and their verification status

Every identifier below is confirmed by the successful compile and link: a wrong function
name, struct field or register alias fails the build. One of them — the `Arduino.h`
include that declares `rp2040` — did fail, and is recorded above.

| API / identifier | Source | Verified exists? |
|---|---|---|
| `pio_add_program`, `pio_claim_unused_sm`, `pio_sm_init` | pico-sdk `hardware/pio.h` | yes (standard SDK) |
| `pio_gpio_init`, `pio_sm_set_consecutive_pindirs` | pico-sdk `hardware/pio.h` | yes |
| `pio_enable_sm_mask_in_sync` | pico-sdk `hardware/pio.h` | yes |
| `pio_get_dreq` | pico-sdk `hardware/pio.h` | yes |
| `sm_config_set_sideset` | pico-sdk `hardware/pio.h` | yes |
| `sm_config_set_fifo_join` | pico-sdk `hardware/pio.h` | yes |
| `dma_claim_unused_channel`, `dma_channel_configure` | pico-sdk `hardware/dma.h` | yes |
| `channel_config_set_ring`, `channel_config_set_chain_to` | pico-sdk `hardware/dma.h` | yes |
| `dma_hw->ch[n].al3_read_addr_trig` | pico-sdk `hardware/dma.h` | yes (RP2040/RP2350 DMA alias registers) |
| `dma_hw->ch[n].al2_write_addr_trig` | pico-sdk `hardware/dma.h` | yes |
| `rp2040.getCycleCount()` | arduino-pico `RP2040Support.h` | yes — but it needs `#include <Arduino.h>` in the translation unit that calls it |
| `clock_gpio_init` | pico-sdk `hardware/clocks.h` | exists, but NOT usable for GP19 on RP2350: `GPOUT` only reaches GPIO 13, 15, 21, 23, 24, 25 |
| `__not_in_flash_func` | pico-sdk `pico/platform/sections.h` | yes — defined as `#define __not_in_flash_func(name) __not_in_flash(__STRING(name)) name`, and proof that it worked is the RAM address of both handlers (result 4) |

## Phase 2 — preamp DSP stage (host-verified, 2026-11-01)

### What was implemented

New file `src/dsp/preamp.h` — header-only, inline, no Arduino/pico headers.
Transfer function: `y = level × tanh(drive × x)`, with default `drive = 4.0`,
`level = 0.85`. Applied to both L and R channels in `process_block`.

### Host test results (`make -C host test`)

```
clang++ -std=c++17 -O2 -Wall -Wextra -I.../src -o harness harness.cpp
./harness --selftest
SELFTEST: PASS (0 failures)
python3 test.py
harness: 48000 frames, 2 ch, 48000 Hz -> out.wav
PASS: preamp engaged — 46000/48000 frames differ, max amp 0.8194 (out.wav = preamp demo, listen to hear saturation)
```

### What the selftest checked

| Check | Method | Result |
|---|---|---|
| `preamp_sample` vs double-precision reference | 41 samples, x ∈ [−1, +1] step 0.05, tolerance 8 float ULPs | PASS |
| Saturation bound at large input (`x = 10`) | `|output − level| < 0.001` | PASS |
| `process_block` output matches `preamp_sample` reference | sample-exact comparison over 32-sample ramp | PASS |
| `waveshape` vs double-precision reference (Phase 1, unchanged) | 41 samples, tolerance 8 float ULPs | PASS |
| `fir_at` moving-average on ramp | expected 24.5, tolerance 1e-4 | PASS |

### WAV test checked

| Check | Result |
|---|---|
| At least half the 48 000 stereo frames differ from the unprocessed input | PASS (46 000/48 000 differ) |
| Max output amplitude ≤ 1.0 (tanh bounded) | PASS (0.8194) |
| Max output amplitude in expected range [0.5, 0.95] for 0.5-amplitude sine at drive=4 | PASS |

`host/out.wav` — preamp-processed 1 kHz stereo sine at 48 kHz / 16-bit. Listen to hear tanh saturation vs the flat `sine.wav` input.

### Firmware build (`pio run -e rpipico2`)

```
RAM:   [          ]   1.9% (used 9960 bytes from 524288 bytes)
Flash: [          ]   1.0% (used 40904 bytes from 4190208 bytes)
========================= [SUCCESS] Took 6.69 seconds =========================
```

Numbers unchanged from Phase 1 — the inline `preamp_block` is folded into the
call chain and adds no observable static footprint.

### Files added or modified

| File | Action |
|---|---|
| `src/dsp/preamp.h` | new — preamp stage: `preamp_sample`, `preamp_block`, default drive/level constants |
| `src/dsp/dsp.h` | modified — `#include "preamp.h"`, `process_block` now calls `preamp_block` |
| `host/harness.cpp` | modified — selftest now checks `preamp_sample` vs double-precision references and `process_block` output; removed the Phase 1 pass-through identity check |
| `host/test.py` | modified — WAV test now checks preamp engagement and bounded amplitude instead of bit-exact identity |
| `docs/BUILD-NOTES.md` | appended — this section |

## Phase 2 — firmware audio path wired (2026-11-01)

`audio_passthrough` in `src/main.cpp` now runs the full preamp chain instead of
`memcpy`. Conversion: 24-bit left-aligned int32 → float (×1/2³¹), `process_block`
(L and R), then clamp→roundf→saturate int24→left-shift 8. Rounding choice:
round-to-nearest (`roundf`); saturation to [-8388608, 8388607] before the shift
prevents int32 overflow on the rare >0 dB path.

### Verified results (real command output, 2026-11-01)

| # | What was run | Result | Evidence |
|---|---|---|---|
| 1 | `pio run -e rpipico2` | **SUCCESS** | RAM 2.0% (10224 / 524288 B), flash 1.0% (41240 / 4190208 B) |
| 2 | `arm-none-eabi-objdump -d firmware.elf \| grep -c vfma` | **hardware FPU in use** | 2 occurrences — no `__aeabi_fmul` anywhere |
| 3 | `arm-none-eabi-nm --print-size firmware.elf` | ISR + callback in SRAM | `dma_irq_handler` @ `0x20000110` (152 B), `audio_passthrough` @ `0x200001a8` (280 B) — both in the `0x2000_0000` region |
| 4 | `make -C host test` | **PASS** | `SELFTEST: PASS (0 failures)`; WAV test: 46000/48000 frames differ, max amp 0.8194 |

Compared to Phase 1: RAM grew +264 B (the float L[]/R[] scratch buffers on the stack
contribute 256 B; remainder is added code); flash grew +336 B. Both within expectations.

### Cycle cost NOT yet measured

The preamp path runs `tanh` per sample — cost will be visible in the `[budget]`
lines at bring-up. The budget constraint is < 80% of 163,840 cycles (131,072) for
the full DSP chain.

### Files added or modified

| File | Action |
|---|---|
| `src/main.cpp` | modified — `audio_passthrough` replaced: int32↔float conversion + `process_block`, `memcpy` and `<string.h>` removed, banner updated to Phase 2 |
| `docs/BUILD-NOTES.md` | appended — this section |

### Blockers / uncertainties — status after the orchestrator's build

1. **PIO instruction encoding** — RESOLVED for the encoding itself. All 28 words were
   re-assembled with `pioasm` and match exactly (table above). Still open at the level of
   *runtime* behaviour: only hardware can show the clocks and the frame alignment are right.

2. **`sm_config_set_sideset` parameter order** — RESOLVED. `sm_config_set_sideset(&cfg_out,
   2, false, false)` compiles against this platform's headers and the program's two-bit
   side-set matches `.side_set 2` in the reference.

3. **`pio_program_t` struct layout** — RESOLVED. The three `struct pio_program` initialisers
   (`{instructions, length, origin}`) compile and link against `pio_add_program`.

4. **DMA alias register names** — RESOLVED. `al2_write_addr_trig` and `al3_read_addr_trig`
   exist in this SDK's `hardware/dma.h`.

5. **`rp2040` global object on RP2350** — RESOLVED, and it is the bug the draft had: the
   object exists, but `i2s.cpp` did not include `<Arduino.h>`. `main.cpp` did, which is why
   Phase 0 compiled.

6. **Underrun/overrun behaviour** — OPEN, by construction. The design cannot hang on a
   FIFO stall: the output program uses `pull noblock` (an empty TX FIFO re-sends the last
   word instead of blocking) and the input program uses `push noblock` (a full RX FIFO drops
   the word). Whether that produces a click rather than a stall is a hardware observation.

7. **The audio callback and the DMA ISR run on Core 0**, in the DMA IRQ handler, with the
   cycle counter read around the callback. Serial output is never done inside the ISR — the
   budget numbers are printed from `loop()` — so a slow USB host cannot stall the audio
   path. This is a design property, not a measurement: the measured cost is what the
   `[budget]` lines will show on hardware.

## Phase 2 — tone stack (host-verified, 2026-11-01)

### What was implemented

New file `src/dsp/tone_stack.h` — header-only, inline, no Arduino/pico headers.
Three RBJ-cookbook biquad stages in series, all direct-form II transposed:

| Band | Type | f₀ | Q/S | Gain range |
|---|---|---|---|---|
| Bass | Low-shelf (S = 1) | 100 Hz | — | −12…+12 dB |
| Middle | Peaking EQ | 800 Hz | Q = 0.707 | −12…+12 dB |
| Treble | High-shelf (S = 1) | 3 kHz | — | −12…+12 dB |

Coefficients computed once by `tone_stack_init(fs, bass_db, mid_db, treble_db)` — no
transcendental math inside the sample loop. `DspState` (added to `dsp.h`) carries the six
`BiquadState` structs (2 per band × 3 bands) between blocks; the caller owns it, no globals,
no allocation.

Firmware: `s_eq` and `s_dsp_state` are file-scope statics in the anonymous namespace of
`src/main.cpp`; `s_eq` is initialised in `setup()` with all bands at 0 dB.

### Host test results (`make -C host test`)

```
clang++ -std=c++17 -O2 -Wall -Wextra -I.../src -o harness harness.cpp
./harness --selftest
SELFTEST: PASS (0 failures)
python3 test.py
harness: 48000 frames, 2 ch, 48000 Hz -> out.wav (bass=0.0 mid=0.0 treble=0.0 dB)
PASS: preamp engaged — 46000/48000 frames differ, max amp 0.8194
harness: 48000 frames, 2 ch, 48000 Hz -> bass_flat.wav (bass=0.0 mid=0.0 treble=0.0 dB)
harness: 48000 frames, 2 ch, 48000 Hz -> bass_boost.wav (bass=12.0 mid=0.0 treble=0.0 dB)
PASS: bass boost — flat=0.1677 boost=0.4219 ratio=8.0 dB
harness: 48000 frames, 2 ch, 48000 Hz -> eq_demo.wav (bass=8.0 mid=0.0 treble=6.0 dB)
eq_demo.wav: 1 kHz stereo sine, preamp + bass +8 dB treble +6 dB — listen and compare to out.wav (preamp only)
```

### What the selftest checked

| Check | Method | Result |
|---|---|---|
| process_block unity EQ matches preamp_sample | 32-sample ramp, tol 1e-5 | PASS |
| Tone stack unity gain (all bands 0 dB) | 2048 samples 1 kHz @ A=0.5, skip 512, peak within 0.5% of A | PASS |
| +12 dB mid peaking boost at 800 Hz | 4096 samples 800 Hz @ A=0.1, skip 1024, measured dB within ±0.5 dB of 12.0 | PASS |
| Stability: ±1 full-scale triangle sweep | 8192 samples through process_block, all outputs ≤ 1.1 | PASS |
| `waveshape`, `fir_at`, `preamp_sample` (unchanged) | as before | PASS |

The +12 dB peaking check uses the RBJ peaking EQ property: gain at the centre frequency is
exactly A² = 10^(dBgain/20), so the expected value is analytically exact (not an estimate).
Double-precision `measured_db` is computed in the selftest; measured value was within 0.01 dB
of 12.0 on this run.

### WAV files generated

| File | Contents |
|---|---|
| `host/out.wav` | Preamp + 0 dB EQ, 1 kHz stereo sine, 48 kHz/16-bit |
| `host/eq_demo.wav` | Preamp + bass +8 dB, mid 0 dB, treble +6 dB — listen to hear V-shape EQ |

### Firmware build (`pio run -e rpipico2`)

```
RAM:   [          ]   2.0% (used 10380 bytes from 524288 bytes)
Flash: [          ]   1.0% (used 41532 bytes from 4190208 bytes)
========================= [SUCCESS] Took 7.01 seconds =========================
```

**Deltas vs Phase 2 preamp baseline (RAM 10224 B / Flash 41240 B):**

| | Baseline (preamp only) | Tone stack added | Delta |
|---|---|---|---|
| RAM | 10224 B (2.0%) | 10380 B (2.0%) | +156 B |
| Flash | 41240 B (1.0%) | 41532 B (1.0%) | +292 B |

The 156 B RAM increase is the six `BiquadState` structs (6 × 8 B = 48 B) plus the
`ToneStackCoeffs` (15 floats × 4 B = 60 B) and `ToneStackCoeffs` init overhead. Flash grows
by 292 B for the three inlined `biquad_sample` loops.

### FPU check

```
vfma.f32 count : 5   (was 2 in preamp-only build)
__aeabi_fmul   : 0   (software float multiply — none, FPU in use)
```

Three additional `vfma.f32` occurrences correspond to the three biquad stages in the
tone-stack hot loop. No soft-float fallback anywhere.

### Files added or modified

| File | Action |
|---|---|
| `src/dsp/tone_stack.h` | new — `BiquadCoeffs`, `BiquadState`, `ToneStackCoeffs`, `ToneStackState`, `tone_stack_init`, `biquad_sample`, `tone_stack_block` |
| `src/dsp/dsp.h` | modified — `#include "tone_stack.h"`, added `DspState`, updated `process_block` signature |
| `src/main.cpp` | modified — `s_eq`/`s_dsp_state` statics, `s_eq` init in `setup()`, pass both to `process_block` |
| `host/harness.cpp` | modified — selftest: 3 new tone-stack checks; `run()` computes `ToneStackCoeffs` once per WAV, accepts optional bass/mid/treble CLI args |
| `host/test.py` | modified — bass-boost amplitude check, `eq_demo.wav` generation |
| `docs/BUILD-NOTES.md` | appended — this section |
