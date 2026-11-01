# Handoff — Zuko (RP2350) — Phase 1 build-verified, prototype parts ordered

> **For the next agent:** This handoff captures the state at session end on 2026-11-01.
> Read this first, then run the verification commands before touching anything. The
> "Critical correction" section matters: the previous handoff points at a repository path
> that no longer exists, and two hardware parts changed after that handoff was written.

**Date:** 2026-11-01 19:47:03 (-03)
**Session energy:** not recorded (no journal was read this session — do not infer it)
**User intent:** build and land Phase 1 (the PIO I2S driver with DMA ping-pong and audio
pass-through), commit it, produce a BOM, and record the parts he then bought for the
prototype.
**Repo:** `/Users/calebe94/Projects/zuko-rp2350`, branch `main`, **no git remote** (local
only, nothing to push to). 3 commits.

---

## TL;DR — Where we are

Phase 1 is complete and **build-verified**, not assumed: the firmware compiles in Docker
(RAM 1.9%, flash 1.0%, `firmware.uf2` 106496 B), the hardware FPU is in use, both the DMA
ISR and the audio callback are confirmed in SRAM, and the 28 hand-assembled PIO opcodes
were cross-checked against `pioasm` and match exactly. The prototype parts were ordered
on 2026-10-31 (R$ 262,39 landed) and are not in hand yet, so **nothing has run on
silicon**: the clocks, the ping-pong buffer alternation and the audio path are unverified.

## Critical correction to the previous handoff

The previous handoff (`2026-10-31_021806-plexi-box-fase0-route-b.md`) is still correct on
architecture but wrong on three points the next agent must not inherit:

- **Previous handoff says:** the repo is `~/Projects/plexi-box-rp2350`; its verification
  block (`cd /Users/calebe94/Projects/plexi-box-rp2350 ...`) uses that path.
  **Reality:** the repository was renamed to **`~/Projects/zuko-rp2350`** on 2026-10-31
  and the old path does not exist. Every command in that handoff with the old path fails.
  It also says "0 commits"; there are now 3.
- **Previous handoff says:** the pots go through a **74HC4051** and the footswitch is an
  **SPST momentary** part.
  **Reality:** what was actually bought is **CD4051BE** (five of them) and a **latching**
  pedal switch set (three). Consequences are in `docs/PINOUT.md` and `BOM.csv`:
  the mux must be powered from **3.3 V** (its VIH at 5 V is ~3.5 V, so 3.3 V GPIO cannot
  drive the select pins reliably) and the switch is read as a **level**, not a press.
- **Previous handoff says:** Phase 1 "has NOT been started".
  **Reality:** Phase 1 is done and build-verified (commits below).

## What's done (commits made in this session)

| SHA | Message |
|---|---|
| `427f9db` | `chore: initial commit — Phase 0 scaffold for the Zuko (RP2350) pedal` (pre-existing) |
| `3a60eef` | `feat: Phase 1 — PIO I2S master, DMA ping-pong and audio pass-through` |
| `c226620` | `docs: add BOM.csv purchase list and document it` |
| `54d9f33` | `chore: sketch the CD4051 channel scan in loop()` — **the user's own edit**, not the agent's: `#include <cstdint>` plus a commented-out 8-channel scan at the end of `loop()`. Leave it; it is his starting point for the Phase 4 ADC scan. |
| *(last commit, see `git log -1`)* | `docs: record the purchased prototype parts` — `BOM.csv` with the real order (`status=bought`, unit prices paid, both substitutions in the notes), the README Procurement section, `docs/PINOUT.md` (CD4051BE at 3.3 V, latching footswitch) and this handoff. |

The working tree is clean after those two commits.

## What's not done (open work)

- **Priority 1: hardware bring-up of Phase 1.** Parts are ordered, not delivered. Nothing
  has run on a board.
- **Priority 2: the analog front end.** The input-buffer op-amp and the low-noise LDO for
  the analog rail were **not** bought and are required to feed the ADC a real signal.
- **Priority 3: Phase 2** (preamp models + tone stack), which first needs the host-native
  test harness that compiles the same DSP sources on the Mac and compares against a
  reference. This does not need hardware and can start today.
- Phase 3: `wav2header.py` + the 1024-tap cabinet FIR. Phase 4: pots via the mux,
  footswitch, OLED, lock-free coefficient updates.
- Never started: KiCad, enclosure layout, priced assembly.
- Not bought (not blocking): SSD1306 OLED, headers, perfboard/build substrate.

## Key decisions made

1. **I2S clocking is route B**: the RP2350 generates MCLK/BCLK/LRCK from a 245.76 MHz
   `clk_sys`, every division integer. Converters are slaves. Not re-litigated.
2. **MCLK cannot use `clock_gpio_init`.** On RP2350 only GPIO 13, 15, 21, 23, 24, 25 reach
   a `GPOUT` clock generator; GP19 is not one of them. MCLK is a 2-instruction PIO state
   machine instead (`set pins,1` / `set pins,0`, divider 10).
3. **Three PIO state machines on PIO0**: MCLK (div 10), output master with 2-bit side-set
   for BCLK/LRCK (div 40), input slave synchronised to those clocks (div 10, entry at
   instruction 4). The single-SM full-duplex option was rejected: with 2 side-set bits
   there is no delay budget left to interleave `in` and `out` on the right clock edges.
4. **Four DMA channels**, not two: full-duplex ping-pong needs a data channel plus a
   control channel per direction (TX data + TX ctrl, RX data + RX ctrl). The control
   channel reloads the data channel's address from a 2-entry ring, so the buffer swap
   costs no handler latency.
5. **Block = 32 samples/channel = 64 words.** One block is 2048 bit-clocks = 666.7 us =
   163840 CPU cycles at 245.76 MHz. Handler cost is instrumented (min/avg/max) in the DMA
   ISR and **printed from `loop()`, never from the ISR**, so a slow USB host cannot stall
   the audio path. No cycle figure has been measured on hardware yet — do not quote one.
6. **Underrun/overrun cannot hang the path**: `pull noblock` (empty TX FIFO re-sends the
   last word) and `push noblock` (full RX FIFO drops the word).
7. **Price policy in `BOM.csv` is enforced by a column**: `quoted` / `reference` / blank
   ("to quote"). No price is ever estimated. The `ORDER` row carries the exact invoice
   amounts; line totals use rounded unit prices.

## Files touched

Created: `src/i2s.h`, `src/i2s.cpp`, `docs/I2S.md`, `BOM.csv`,
`.hermes/prompts/2026-10-31_phase1-pio-i2s.md`, this handoff.
Modified: `src/main.cpp`, `README.md`, `docs/BUILD-NOTES.md`, `docs/README.md`,
`docs/PINOUT.md`.

## Verification commands

```bash
cd ~/Projects/zuko-rp2350

# 1. State: 3 commits, tree state known
git log --oneline
git status --short

# 2. Build really works (needs Docker running; the volume caches the Arduino core)
docker run --rm -v zuko-pio-cache:/root/.platformio \
  -v "$PWD":/workspace -w /workspace zuko-rp2350-build pio run -e rpipico2
# expect: SUCCESS, RAM 1.9% (9960 B), flash 1.0% (40904 B)

# 3. FPU + RAM placement proof
docker run --rm -v zuko-pio-cache:/root/.platformio -v "$PWD":/workspace -w /workspace \
  zuko-rp2350-build bash -lc '
  TC=/root/.platformio/packages/toolchain-rp2040-earlephilhower/bin
  $TC/arm-none-eabi-objdump -d .pio/build/rpipico2/firmware.elf | grep -c -w vfma.f32   # >0
  $TC/arm-none-eabi-objdump -d .pio/build/rpipico2/firmware.elf | grep -c __aeabi_fmul   # 0
  $TC/arm-none-eabi-nm --print-size --defined-only .pio/build/rpipico2/firmware.elf \
    | grep -iE "dma_irq_handler|audio_passthrough"   # both at 0x2000....
  '

# 4. BOM parses and is internally consistent
python3 -c "import csv;r=list(csv.reader(open('BOM.csv',encoding='utf-8')));print(len(r),'rows');print(all(len(x)==10 for x in r if x))"
```

## Known issues / blockers

- **No hardware in hand.** Parts ordered 2026-10-31 at Mercado Livre; R$ 244,01 of items,
  R$ 20,34 discount, free freight, R$ 38,72 import costs, **R$ 262,39 landed**.
- **Two parts differ from the plan** (CD4051BE instead of 74HC4051; latching switch
  instead of momentary). Firmware and docs already updated for both.
- **The analog front end cannot be tested without** the op-amp and the low-noise LDO.
  Feeding a sine through a resistor bias network is a stopgap for a first audio check.
- **PCM1808 modules arrive untested.** Check the `MD0`/`MD1` straps are at their power-on
  default (both low = slave), and that `SCK` is broken out as an input on the module — a
  module hard-strapped to master mode has to be modified.
- **Verify the Pico board on arrival**: BOOTSEL drive name and the clock banner must show
  an RP2350 with 4 MB flash. A "Pico 2 W" or a clone changes nothing in the firmware but
  should be known.
- **Never enable internal pull-downs** on this chip (silicon erratum: the pin latches low
  after the first read). Pull-up or external resistor only.
- The `-mfloat-abi=hard -mfpu=fpv5-sp-d16` flags from the original brief break this
  platform's build; they are deliberately absent from `platformio.ini`.

## Next actions (suggested order)

1. **Start Phase 2's host harness now** — it needs no hardware: compile the DSP sources
   with the host compiler, run a WAV through them, compare against a reference. Without
   it, the preamp models cannot be checked at all before the board works.
2. **Order the missing parts**: input-buffer op-amp (TL074 or MC33079) and a low-noise LDO
   for the analog rail; add the OLED, headers and perfboard to the same order.
3. **When the parts arrive, bring-up in this order**: rails first (3.3 V digital, 5 V for
   the PCM1808 analog side, separate analog rail) → scope GP19/GP21/GP22 for 12.288 MHz /
   3.072 MHz / 48 kHz → confirm the `[budget]` lines print → feed a 1 kHz sine and look for
   it at the DAC output.
4. **Then replace the "not yet measured" section of `docs/I2S.md` with the real cycle
   numbers** from the `[budget]` lines, and record them in `docs/BUILD-NOTES.md` as
   measured-on-silicon evidence.
5. Only then Phase 2's preamp models, with the harness already in place.

## Open questions for the user

- Commit the pending changes (BOM update, README, PINOUT, and his own `main.cpp` sketch) as
  one commit, or split the hardware record from his code edit?
- Does he want the mux scan written now (Phase 4 pulled forward) or left as his sketch?
- For the analog rail: prefer a specific low-noise LDO (needs a decision before buying).

---

## Context for the next session

### Background

The user is a senior software engineer with an embedded background, building this pedal as
both a tool and a portfolio artifact. He reads documentation closely and reacts badly to
over-claiming: keep "verified" and "expected" strictly separated, and never present a
community estimate or a computed number as a measurement. He has zero tolerance for
uncontrolled API spend — prefer local (R$ 0) or prepaid with a cap. He is neurodivergent
(TDAH, autism level 1, TAG): answer in Brazilian Portuguese, short complete sentences,
tables over prose, no "simplesmente"/"obviamente"/"é só", and give a decision rule instead
of an open menu.

The working pattern that produced this session: Hermes writes a precise English prompt to
`.hermes/prompts/`, runs the Claude Code CLI as the worker (`claude -p`, free on the Pro
plan), then **independently verifies** the artifact. That verification is not a formality:
this session the Claude draft did not compile (`rp2040` used without `#include
<Arduino.h>`), claimed "BUILD NOT EXECUTED" honestly, and also silently stripped the Phase
0 comments out of `main.cpp` — both were caught and fixed by the orchestrator.

Practical note: the first worker run (Opus, `--effort high`) stalled for 35 minutes with
zero file writes and had to be killed. The relaunch with `--effort medium` plus an explicit
"write the files first, keep analysis short" note finished the job in about 20 minutes.
If a `claude -p` run sits at 0 bytes of output and no changed files, check
`~/.claude/projects/<slug>/*.jsonl` for progress before waiting longer.

### Key URLs / endpoints

- Platform: https://github.com/maxgerhardt/platform-raspberrypi · core: https://github.com/earlephilhower/arduino-pico
- I2S reference implementation (master/slave, PIO programs): https://github.com/malacalypse/rp2040_i2s_example
- CMSIS-DSP for Arduino: https://github.com/teamprof/ArduCMSIS-DSP

### Relevant docs

- `docs/I2S.md` — the driver as built: state machines, DMA topology, block budget, the
  hardware acceptance tests (read this before touching the driver).
- `docs/BUILD-NOTES.md` — verified evidence per phase, including the PIO opcode cross-check.
- `docs/PINOUT.md` — authoritative pin map; now also carries the CD4051BE and latching
  switch notes.
- `BOM.csv` — what was bought, at what price, and what is still missing.
- `src/i2s.cpp` — the PIO programs are hand-assembled `uint16_t` arrays; if anything about
  them changes, re-assemble with `pioasm` and compare words.

### People / stakeholders

None. Personal project.
