# Handoff — Zuko (RP2350) — Phase 0 done and verified, I2S clock route decided

> **RENAME NOTE (added 2026-10-31, after this handoff was written):** the project was renamed from
> "Plexi Box (RP2350)" to **"Zuko (RP2350)"** and the repository moved from
> `~/Projects/plexi-box-rp2350` to **`/Users/calebe94/Projects/zuko-rp2350`**. Everything below that says
> "Plexi Box" or the old path means this same repo. The vault note is now
> `~/Notas/1 - Projects/Zuko (RP2350).md`. Reason: the RP2350 pedal is the digital implementation of the
> same Marshall Plexi concept as the existing **analog** Zuko ([[Zuko]], `~/Projects/zuko`), so both
> share the Avatar name and are told apart by the parenthetical, like `Toph (DAP)` / `Toph (Pedal)`.

> **For the next agent:** This handoff captures the state at session-end on 2026-10-31 02:18.
> Read this first, then run the verification commands before touching anything. There are NO
> commits in this repository — every file is untracked, deliberately. The most important thing
> to read is the "Critical correction" section below, because the sibling project's docs
> (`plexi-mcu`) contain a verdict about the RP2350 that does NOT apply to this project.

**Date:** 2026-10-31 02:18:06 (-03)
**Session energy:** high
**User intent:** Start a new RP2350-based pedal project (the "Plexi Box"): a digital stompbox that
models a Marshall 1959 Super Lead and a JCM800 analytically, an FMV tone stack, a presence filter
and a Marshall 1960B 4x12 cabinet IR. Delegating the heavy lifting to the Claude Code CLI, with
Hermes orchestrating and verifying. Two open decisions had to be closed along the way (I2S clock
topology, and how to read six pots from a chip with four ADC pins).
**Repo:** /Users/calebe94/Projects/plexi-box-rp2350 (git initialised, branch `main`, **0 commits**)
**Sibling projects:** /Users/calebe94/Projects/plexi-mcu (Daisy Seed, different architecture, untouched),
/Users/calebe94/Projects/toph (Daisy Seed, backing-track player, untouched)

---

## TL;DR — Where we are

Phase 0 is complete AND verified by real execution: the firmware builds inside Docker with
`pio run -e rpipico2`, producing a 99,840-byte `firmware.uf2` at 1.7% RAM and 0.9% flash usage.
This was verified TWICE: once with the original 250 MHz value and again AFTER the clock was changed
to 245.76 MHz (route B), which is the configuration in the tree now — the second run took 255.07 s and
produced byte-identical memory figures. The
hardware FPU is confirmed in the linked ELF (`vfma.f32`/`vmul.f32` present, zero `__aeabi_fmul`), and
the CMSIS-DSP `arm_fir_f32` symbols are confirmed present and linking. There is no I2S, DSP or UI code
yet. The I2S clock topology was decided (route B: the RP2350 generates MCLK/BCLK/LRCK; the converters
are slaves) and the pin map, `platformio.ini`, README, vault note and diagram were all updated for it.
No hardware has been bought. Phase 1 (PIO I2S driver with ping-pong DMA) has NOT been started.

## Critical correction to the previous handoff

There is no earlier handoff in this repository. The correction is needed for the **sibling** project's
handoff and docs, which a next agent is likely to read because both projects are "a Marshall pedal with
a microcontroller":

- **The `plexi-mcu` docs say (verbatim, `docs/01-architecture.md`):** "| ESP32-S3 / RP2350 | Cheap,
  decent RAM | Not enough DSP headroom for NAM + IR; thinner audio ecosystem | No |"
- **Reality:** that verdict is about running **NAM (a neural network) plus IR convolution**. This
  project deliberately does NOT use NAM: the preamp is modelled analytically (tanh triode waveshapers
  with bias and cathode sag), which is a small fraction of the arithmetic. The RP2350A has a hardware
  single-precision FPU and ARMv8-M DSP extensions, so the analytic chain plus a 1024-tap FIR is
  comfortable — the community reference numbers put a 1024-tap direct FIR at ~24.6% of a 250 MHz core.
- **Verified by:** the FPU proof and the CMSIS-DSP link proof in `docs/BUILD-NOTES.md` (real command
  output, reproduced by `.hermes/verify-2.sh`). The CPU *load* of the full DSP chain is NOT yet
  measured — that is planned work, not a verified claim.
- **Implications for the next agent:**
  1. Do not resurrect the "RP2350 can't do it" verdict. It was about NAM.
  2. Do not propose NAM here. If the user ever asks for NAM, that reopens the platform question.
  3. If you need the analytical preamp to be accurate rather than merely plausible, the honest
     blocker is tuning data, not CPU.

## What's done (commits made in this session)

**No commits.** The repo was initialised with `git init` and nothing was ever committed — on purpose,
so the user can review everything first. All files are untracked.

Created (repo):
- `platformio.ini` — board `rpipico2`, `board_build.f_cpu = 245760000L` (see decisions), library deps,
  and the compiler-flag block with the hard-float finding documented in place
- `Dockerfile` — `python:3.11-slim-bookworm` + PlatformIO; installs the platform with
  `pio pkg install --global --platform <url>`
- `.github/workflows/build.yml` — build, `-t size`, artifact `plexi-box-firmware`; caches `~/.platformio`
- `src/main.cpp` — Phase 0 skeleton: sets vreg 1.20 V, sets the system clock, prints configured
  `F_CPU`, measured clock (`frequency_count_khz`), vreg code, a `tanhf` waveshaper and a 32-tap FIR
  with cycles/MAC; blinks GP15; Core 1 heartbeat via `setup1()`/`loop1()`
- `docs/PINOUT.md` — authoritative pin map, route B clock table, mux channel map, pull-down erratum,
  power rails, the 2.5 V ADC bias warning
- `docs/README.md` — documentation index
- `docs/BUILD-NOTES.md` — the verified evidence table, both bugs found, how to reproduce
- `README.md`, `.gitignore`, `scripts/README.md` (placeholder for the future `wav2header.py`)
- `.hermes/prompts/` — the three prompt files used to drive the Claude Code CLI (phase 0, diagram fix,
  diagram route B)
- `.hermes/probe-flags.sh`, `.hermes/verify-2.sh` — verification scaffolding (not deliverables)

Created (Obsidian vault, pt-BR):
- `~/Notas/1 - Projects/Plexi Box (RP2350).md` — project hub built from `Templates/Project.md`
- `~/Notas/1 - Projects/Plexi Box (RP2350).excalidraw` — component-connection diagram

## What's not done (open work)

- **Priority 1: Phase 1 — the PIO I2S driver with ping-pong DMA and audio pass-through.** The prompt
  has not been written yet. This is the next unit of work.
- Priority 2: hardware procurement. The user has only op-amps at home; every other part must be bought
  (see "Known issues" for the list) and there is no priced BOM yet.
- Priority 3: Phase 2 (preamp models + tone stack with host-native tests), Phase 3 (`wav2header.py` +
  FIR convolution), Phase 4 (pots via mux, footswitch, OLED, lock-free coefficient updates).
- Never started: enclosure/KiCad, analog front-end schematic, power supply design.
- Not verified anywhere: whether the firmware actually runs on a Pico 2 (no board exists).
- `src/cab_ir_1960b.h` and `scripts/wav2header.py` do not exist yet (Phase 3).

## Key decisions made

1. **I2S clock = route B: the RP2350 generates everything.** `clk_sys = 245.76 MHz` exactly, so
   `MCLK = clk_sys/20 = 12.288 MHz` (GP19 -> PCM1808 SCK), `BCLK = clk_sys/40 = 3.072 MHz` (GP21) and
   `LRCK = 48 kHz` (GP22, counted from 32 bits per channel). All divisions are integers, so no
   fractional-divider jitter. The PCM1808 and the PCM5102A are slaves. Rejected route A (a dedicated
   12.288 MHz oscillator, ~R$20) because it forces the PIO into slave mode — the hard, thinly
   documented path — while this project already carries the DSP risk. Route A stays reachable: both
   feed the same SCK node.
   **This decision is why `f_cpu` is 245.76 MHz and NOT the brief's 250 MHz.**
2. **Six pots through one 74HC4051 into ADC0 (GP26)**, selects on GP6/GP7/GP8. The chip has only four
   ADC-capable pins (GP26-GP29) and six pots would need six. Mux channel map: Y0 Gain, Y1 Bass,
   Y2 Middle, Y3 Treble, Y4 Presence, Y5 Master. GP27 stays free for a future expression pedal.
3. **The brief's `-mfloat-abi=hard -mfpu=fpv5-sp-d16` are removed.** They break the build (verified:
   `bits/c++config.h: No such file or directory` after 165 s, because the toolchain installed for the
   arduino-pico core ships no hard-float multilib). `softfp` is used instead and the hardware FPU is
   proven still in use by objdump. Details in `docs/BUILD-NOTES.md`.
4. **`-ffast-math` deliberately deferred** until its effect on `tanhf()` accuracy is measured.
5. **v1 scope:** preamp models + tone stack + cab IR + 6 knobs + 1 footswitch (channel toggle) + OLED.
   No presets, no MIDI, no true bypass.
6. **The cabinet IR and the header generated from it are NOT committed.** Unclear redistribution
   licence; the repo is a public portfolio piece. `.gitignore` covers `*.wav` and `src/cab_ir_*.h`.
7. **The dev IR is** `/Users/calebe94/Downloads/Marshall 1960B 412-G12M Greenback.wav` — 48 kHz,
   24-bit, mono, 8192 samples (170 ms). The converter will truncate to 1024 taps, so no resampling is
   needed. Copy it outside `~/Downloads` before relying on it.
8. **Delegation config changed to R$0:** `delegation.provider = llama-server`,
   `delegation.model = qwen3-coder-30b-local`. It used to point at `gemini-3.6-flash` (free tier,
   ~20 requests/day), which killed a subagent mid-run. Verified working with a smoke test (29.45 s).
   The local model is slow in agent loops — prefer `claude -p` (free on the Pro plan) for reasoning
   work and plain commands for deterministic work.

## Files touched

Created: everything in "What's done" above. Modified: `platformio.ini`, `README.md`,
`docs/PINOUT.md`, `src/main.cpp` (the last one only by the Phase 0 generator). Deleted: nothing.

Outside the repo, the vault files above were created. Nothing else in `~/Notas` was touched by this
session (the vault has ~55 pre-existing modified and ~223 untracked files; do not "clean" them).

## Verification commands

```bash
# 1. Repo state: git initialised, ZERO commits, everything untracked
cd /Users/calebe94/Projects/plexi-box-rp2350
git log --oneline 2>&1 | head -3     # expect: "does not have any commits yet"
git status --short | head

# 2. The build really works (first run downloads the core toolchain, ~200 MB)
docker build -t plexi-box-build .
docker run --rm -v "$PWD":/workspace plexi-box-build
# expect: "SUCCESS", RAM 1.7%, flash 0.9%
ls -la .pio/build/rpipico2/firmware.uf2        # expect 99840 bytes

# 3. FPU + CMSIS-DSP proof (re-runs both: prints instructions and greps the archive)
docker run --rm -v "$PWD":/src plexi-box-build bash /src/.hermes/verify-2.sh
grep -ao "arm_fir_[a-z0-9_]*" \
  .pio/libdeps/rpipico2/ArduCMSIS-DSP/src/cortex-m33/libCMSISDSP.a | sort -u

# 4. Clock is 245.76 MHz, not 250
grep -n "f_cpu" platformio.ini

# 5. Delegation costs nothing
hermes config get delegation     # expect provider llama-server, model qwen3-coder-30b-local

# 6. Vault deliverables exist
ls -l "/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).md" \
      "/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).excalidraw"
```

Claims to re-verify rather than trust: the CPU load figures for the DSP chain (community estimates,
never measured here), the availability of a crystal-less PCM1808 module in Brazil, prices, and the
assumption that the module's MD0/MD1 straps are at their chip default.

## Known issues / blockers

- **No hardware.** Everything physical is blocked on buying: Pico 2 (RP2350A), a PCM1808 module
  (must have SCK broken out; the cheap one has NO crystal, which is fine for route B), a PCM5102A
  module, a 74HC4051, an SSD1306 0.96" OLED, 6 x B10K linear pots, an SPST momentary soft-touch
  footswitch, P10 jacks, a DC jack, a 1590BB enclosure, LDOs and passives. No priced BOM exists yet;
  the vault has a procurement workflow and a `hardware-component-procurement` skill.
- **The Docker image does not bake in the Arduino core's toolchain**, so the first build in a fresh
  container downloads ~200 MB. CI caches `~/.platformio`. Left as-is on purpose (documented).
- **The PCM1808 module's straps must be checked on arrival.** Route B needs slave mode, which is the
  chip's power-on default, but a module hard-strapped to master would have to be modified.
- **The `.excalidraw` diagram WAS regenerated for route B and verified** (133 elements, 28 arrows).
  Verified geometrically from the arrow endpoints, not from the renderer (the official preview script
  does not draw arrow elements): the three clock arrows — `GP19 -> SCK (MCLK 12,288 MHz)`,
  `GP21 -> BCK (3,072 MHz)`, `GP22 -> LRCK (48 kHz)` — now run RP2350 -> PCM1808, while the data arrow
  `DOUT -> GP20 (I2S DIN)` still runs PCM1808 -> RP2350. The ADC box title reads
  `PCM1808 — ADC 24-bit I2S (escravo)` and the old "open decision" card is now
  `Clock I2S — rota escolhida: RP2350 master`.
- **The Obsidian Excalidraw plugin rewrites the file on its own.** Verified: a file generated at
  91,791 B / 127 elements was rewritten by plugin 2.23.7 to 114,680 B / 128 elements, with an injected
  `image` element and a non-conforming index (`a2`) appended. Treat the generated `.excalidraw` as a
  proposal: the durable artifact is the generator script (`/tmp/plexi_work/gen_plexi_v2.py`, move it
  somewhere permanent), and the user must CLOSE the tab before regenerating.
- **A stray broken `image` element is still in the diagram** (`Um4xXAk3`, `status: pending`, no image
  data, 1079x1079 placed at x≈2673 / y≈1005). It is an Obsidian paste artifact; it inflates the canvas
  to 3812x2196 and renders as empty space in the lower right. Delete it in Obsidian, or from a script
  while the tab is closed.
- There is also a stale orphan `/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).svg` (an export
  made by the plugin before route B). The note does NOT reference it — it embeds the `.excalidraw`
  directly. Safe to delete, but ask the user first.
- **The vault diagram embed is a plain transclusion** (`![[...excalidraw|9999]]`), which is the
  pattern the `excalidraw-obsidian-integration` skill validates. Do not add `excalidraw-plugin:`
  frontmatter keys to the note unless a verified `# Excalidraw Data` block is added too.
- **Never enable internal pull-downs on this chip** (silicon erratum: the pin latches low).

## Next actions (suggested order)

1. **Write the Phase 1 prompt** and run it through `claude -p` with the same pattern used here: prompt
   written to a file under `.hermes/prompts/`, launched in the background, verified afterwards by the
   orchestrator (never trust the child's summary). Scope: a PIO I2S program in MASTER mode generating
   BCLK/LRCK on GP21/GP22 plus MCLK on GP19, full duplex (DIN GP20, DOUT GP23), two DMA channels in a
   ping-pong configuration over 32-sample blocks, and an audio pass-through path from ADC to DAC
   handled on Core 0. Acceptance: builds in Docker, and the design states a measured block-processing
   budget rather than a guess.
2. **Prepare the host-native test harness** (compiles the same DSP sources on the Mac, runs a WAV
   through them, compares against a reference) BEFORE writing the preamp models — it is the only way
   to verify the DSP without hardware.
3. Produce a priced BOM for the purchase list above (reuse the vault procurement workflow).
4. Only then run Phase 2 (preamp + tone stack) with the harness in place.
5. Defer `wav2header.py` and the FIR to Phase 3; the dev IR path is in the decisions above.

## Open questions for the user

- Does the repo get its first commit now, or does the user want to review the tree first?
- Does the project keep the name "Plexi Box (RP2350)" and the path `~/Projects/plexi-box-rp2350`?
  The other pedals in the vault use Avatar names (Zuko, Toph, Iroh) if a rename is wanted.
- Should the diode/analog front-end schematic be drawn in KiCad now, or after Phase 1 passes?

---

## Context for the next session

### Background

The user is a senior software engineer with an embedded background who is deliberately building this
pedal as both a guitar tool and a portfolio artifact. He reads documentation closely and reacts badly
to over-claiming: keep the difference between "verified" and "expected" explicit, and never present
a community estimate as a measurement.

The working pattern that produced this session's results: Hermes does reconnaissance and decision
framing in pt-BR, writes a precise English prompt to a file, runs the Claude Code CLI in the
background, and then INDEPENDENTLY verifies the artifact (real build, real disassembly, real JSON
validation) before reporting. Twice in this session the child's work looked plausible and was not:
once the Dockerfile installed nothing because `-p` means `--project-dir`, and once the diagram came
back as a text board instead of a connection diagram.

The user's hard constraints, which must survive into future sessions: zero tolerance for uncontrolled
API spend (prefers local R$0 or prepaid with a cap); answers in Brazilian Portuguese; code, comments
and repo documentation in English; no over-engineering; and a clear "definition of done" for every
task. Words to avoid in Portuguese text: "simplesmente", "obviamente", "é só".

### Key URLs / endpoints

- Platform: https://github.com/maxgerhardt/platform-raspberrypi · core: https://github.com/earlephilhower/arduino-pico
- CMSIS-DSP for Arduino: https://github.com/teamprof/ArduCMSIS-DSP
- I2S reference implementation (slave/master discussion): https://github.com/malacalypse/rp2040_i2s_example
- Local model endpoint: http://127.0.0.1:8131/v1 (llama-server, alias qwen3-coder-30b-local)

### Relevant docs

- `README.md` — status and layout, in the repo
- `docs/PINOUT.md` — pin map, route B clock table, mux map, power rails
- `docs/BUILD-NOTES.md` — verified evidence and both bugs; read before touching the build
- `~/Notas/1 - Projects/Plexi Box (RP2350).md` — the user's entry point, pt-BR
- `~/Projects/plexi-mcu/docs/01-architecture.md` — the sibling project's platform comparison; read it
  only to understand why its RP2350 verdict does NOT apply here (see Critical correction)

### People / stakeholders

None. Personal project. (The user's girlfriend Laura and his stepfather Eliel appear in other vault
contexts, not in this project.)

---

## Quick resume prompt

> Retomar o projeto **Plexi Box (RP2350)**: pedal digital de guitarra no Pico 2 que modela o Marshall
> 1959 Plexi e o JCM800 (modelagem analítica, sem NAM), tone stack FMV, presença e IR de gabinete
> 1960B por FIR de 1024 taps. A **Fase 0 está concluída e verificada** (build real no Docker,
> `firmware.uf2` de 99.840 bytes, FPU de hardware confirmada por objdump, CMSIS-DSP linkando). O
> estado está no repo `~/Projects/plexi-box-rp2350` (leia `.hermes/handoff/2026-10-31_021806-plexi-box-fase0-route-b.md`,
> depois `docs/PINOUT.md` e `docs/BUILD-NOTES.md`) e a nota de entrada é
> `~/Notas/1 - Projects/Plexi Box (RP2350).md`. Decisão de clock fechada: rota B, o RP2350 gera
> MCLK/BCLK/LRCK com `clk_sys` de 245,76 MHz e os conversores são escravos. Não há commits no repo e
> nenhum hardware comprado. Próximos passos: (1) escrever e rodar o prompt da Fase 1 (driver PIO I2S
> em modo master com DMA ping-pong e pass-through de áudio), (2) montar o harness de teste nativo no
> Mac antes dos modelos de pré, (3) levantar a BOM com preços para a compra.
