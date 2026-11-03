# Handoff — Zuko (RP2350) — full host loop live: rt simulator, JCM800, 0-100 presets

> **For the next agent:** This handoff captures the state at session-end 2026-11-02.
> Read this first, then verify state with the listed commands, then continue.
> It supersedes `2026-11-01_harness-landed-fase2-dsp.md` (which is still correct on
> Phases 1-3 history but predates: the real-time host simulator, the JCM800 model,
> the 0-100 preset scaling, and the top-level Makefile — see Critical correction).

**Date:** 2026-11-02 23:36 (-03)
**Session energy:** medium
**User intent:** keep the execution loop running (plan -> worker -> verify -> commit)
while the hardware has not arrived; make the firmware's DSP audible/testable on the
Mac (offline harness AND real-time through his Tank G pedal as USB interface).
**Repo:** `/Users/calebe94/Projects/zuko-rp2350`, branch `main`, no remote. Head `9110ed3`.

---

## TL;DR — Where we are

The whole Phase 2+3 DSP chain (JCM800-style preamp, tone stack, cab-sim FIR) runs
THREE ways with the same `src/dsp/` sources: (1) offline in the host harness
(WAV in/out), (2) LIVE on the Mac — guitar -> Tank G USB interface -> same chain ->
output, ~11 ms felt latency on the pedal's headphone out, (3) still build-verified
in the firmware (not on silicon; parts ordered 2026-10-31, not delivered). All
parameters are configured through `.ini` presets on an intuitive 0-100 scale and
the user has approved the sound ("ficou lindo"). The user tunes presets himself;
`host/presets/jcm800-punk.ini` is his live-tweaked file and is intentionally left
uncommitted when it differs from HEAD.

## Critical correction to the previous handoff

- **Previous handoff said:** next step 1 was "Phase 2 DSP, host-first" and the
  firmware default model was the generic tanh preamp.
- **Reality (verified by `make test` + `git log`):** Phases 2 AND 3 are done
  host-side and committed (`fc0c4f8`, `140803f`); the default drive stage is now
  SELECTABLE (`AmpModel::kGeneric | kJcm800` in `src/dsp/dsp.h`); presets exist.
- **Implications:**
  - Do not re-plan Phase 2/3 DSP work — it is the tested baseline now.
  - The firmware binary does NOT include the JCM800 path (not referenced by
    `main.cpp` yet); flash is unchanged on purpose.
  - `cabsim_taps` is a runtime parameter now; the IR header max is 512 taps.

## What's done (commits this session, chronological)

| SHA | Message |
|---|---|
| `f7824ea` | Blitzkrieg-Bop-inspired demo riff generator (Karplus-Strong + drums, stdlib) |
| `29c17be` | JCM800-style analytical amp model + host INI presets |
| `3fcabc2` | Real-time host simulator (PortAudio duplex) |
| `ae38608` | rt device selection accepts numeric index or name substring |
| `f99bdc1` | rt error message names which device selection failed |
| `b87fd2a` | rt splits input/output into two streams across different clocks |
| `1b6e725` | jcm800-fat preset |
| `1b187ab` | jcm800-punk preset; extended gain range documented |
| `8f286fb` | ALL .ini parameters on a 0-100 scale (shared `host/ini_map.h`) |
| `e25f217` | untrack the rt binary |
| `27b13bf` | lower rt latency (64-frame buffers, 4 ms) + 512-tap cab IR; .gitignore hygiene |
| `c6dc3b4` | `cabsim_taps` as a .ini parameter (runtime FIR length) |
| `5bf8c4c` | top-level Makefile |
| `a0359ff` | root targets delegate to sub-Makefiles; `host` alias |
| `9110ed3` | loudness-compensated cabsim_taps (L2) + fixed harness header deps |

## What's not done (open work)

- **Priority 1: hardware bring-up** — parts ordered 2026-10-31 (Mercado Livre,
  R$ 262,39 landed), NOT delivered. Nothing has run on silicon. Bring-up order in
  `2026-11-01_194703-zuko-fase1-pecas-compradas.md`.
- **Priority 2: user purchases** U5 (TL074/MC33079 op-amp) + U6 (LD1117-5V stopgap,
  R$ 2,52; sources in BOM.csv U6 row). U6a (TPS7A4700 module) only if the measured
  noise test proves the 1117 rail audible.
- Wire the JCM800 model into the firmware path (currently selectable in `src/dsp/`
  but `main.cpp` uses the generic preamp; user has not chosen the firmware default).
- Phase 4 (pots via CD4051, latching footswitch, OLED) — not started beyond the
  user's commented sketch in `main.cpp` loop().
- Social-media build log: DEFERRED by user; revisit only when he brings it up.
  Chosen rhythm: real-time build log on "anti-social" networks; platform undecided.

## Key decisions made

1. **Worker pattern:** Claude CLI (`claude -p --model claude-sonnet-4-6 --effort
   high --allowedTools "Bash,Read,Write,Edit,Glob,Grep"`), prompt written to a file,
   run in background with notify, output to a log, orchestrator verifies INDEPENDENTLY
   and commits. Cost R$ 0 on Pro plan. OpenRouter credits (US$ 12) untouched as reserve.
2. **No NAM in this project** (standing rule). Analytical DSP only.
3. **Tank G routing that works:** input = PortAudio device `[1]` "USB-Audio" (in:2),
   output = device `[0]` "USB-Audio" (out:2, pedal headphone) — same name, two
   devices; same-hardware duplex stream, 5.9 ms reported. Different output devices
   (e.g. speakers `[3]`) use the split-clock two-stream mode (~18 ms reported).
4. **CoreAudio err=-50:** a single duplex stream cannot span two USB clock domains.
   Fixed by two streams + lock-free SPSC ring (underrun = silence, overflow = drop).
5. **All .ini parameters 0-100** (user request): EQ 50 = neutral (+/-12 dB ends),
   presence/master = percent, gain 0-100 -> per-stage drive 2..50, and gain > 100 is
   INTENTIONAL (tanh-bounded) — user discovered drive ~49 "punk wall" and loves it.
6. **cabsim_taps is loudness-compensated (L2)** so A/B on latency is honest. DC-sum
   compensation over-boosts mids/HF (lowpass IR carries LF in the tail) — do not
   revert to it. Header max = 512 taps; group delay = taps/2 samples.
7. **Cab IR is 512 taps now** (was 1024; 56.97% energy at that truncation of the
   1024 design). User judged the resulting sound "lindo". Regenerate:
   `python3 scripts/wav2header.py --analytic --taps N -o src/dsp/cab_ir.h`.
8. **User tunes presets himself** — leave `host/presets/jcm800-punk.ini` dirty; do
   not commit his live tweaks unasked, do not "fix" his values.
9. Commits during the loop are pre-authorized by the user; no remote, never push.

## Files touched (created or heavily modified this session)

Created: `src/dsp/jcm800.h`, `host/ini_map.h`, `host/rt/{rt.cpp,Makefile,.gitignore}`,
`host/presets/{clean,crunch,jcm800,jcm800-fat,jcm800-punk}.ini`,
`scripts/demo_blitzkrieg.py`, root `Makefile`, this handoff.
Modified: `src/dsp/{dsp.h,cab_sim.h,cab_ir.h}`, `src/main.cpp` (Phase 2 path),
`host/{harness.cpp,test.py,Makefile,.gitignore}`, `docs/BUILD-NOTES.md`, `BOM.csv`.

## Verification commands

```bash
cd ~/Projects/zuko-rp2350
git log --oneline -3 && git status --short   # head 9110ed3; punk.ini may be dirty (user's)
make test        # host selftest + WAV checks: expect 3 PASS lines
make firmware    # Docker build: SUCCESS, RAM ~2.0%, flash ~1.1%
make verify      # + vfma.f32 > 0, __aeabi_fmul == 0, ISR/callback at 0x2000....
make devices     # Tank G = two entries named "USB-Audio" (out:[0], in:[1])
make live        # real-time; needs the pedal plugged via USB
```

## Known issues / blockers

- **No hardware in hand** — bring-up blocked on delivery.
- Firmware binary does not include the JCM800 path yet (by design, awaiting the
  user's default-model choice).
- `host/presets/jcm800-punk.ini` currently dirty (`cabsim_taps = 1024` clamps to
  512; his live tuning). Leave it alone unless he asks.
- OpenRouter has US$ 12 credits — use ONLY if Claude CLI can't do a task; never
  post-paid models (user trauma with uncontrolled API spend).

## Next actions (suggested order)

1. When the parts arrive: bring-up in the order of the 2026-11-01 handoff
   (rails -> scope GP19/21/22 -> `[budget]` lines -> 1 kHz sine pass-through),
   then replace the "not yet measured" sections of `docs/I2S.md` with real numbers.
2. Ask the user whether the firmware default model should be jcm800; if yes, wire
   `AmpModel::kJcm800` + a compile-time default config into `src/main.cpp`, run
   `make verify`, watch the `[budget]` delta on hardware.
3. Phase 4 (pots/footswitch/OLED) only after the audio path is verified on silicon.
4. Offer a skill save for the "claude-CLI worker loop" pattern if reused again.

## Open questions for the user

- Firmware default amp model: generic tanh or jcm800? (blocks wiring it into
  `main.cpp`; costs flash and cycles)
- Does he want the punk preset (his current tuning) committed as-is once stable?

---

## Context for the next session

### Background

The user is a senior engineer (ND: TDAH, autism level 1, TAG) building this pedal
as a portfolio artifact. Answer in pt-BR, short complete sentences, tables over
prose, never "simplesmente/obviamente/é só", give a decision rule plus plan B.
He reacts badly to over-claiming: keep "verified" vs "expected" strictly separated;
never present computed numbers as measurements. Confirm before irreversible actions.
Code/comments in ENGLISH; conversation in pt-BR.

The Tank G (M-Vave) works as a class-compliant USB interface on macOS; the user
routes its output to headphones and already used it with GarageBand/Audacity.

Felt latency today: ~11 ms (pedal headphone out, 512 taps), ~23 ms (Mac speakers,
split-clock). Under ~12 ms most players don't notice; the floor is the Tank G's
USB driver, not our code. On the RP2350 the same chain should be ~7 ms.

### Key URLs / endpoints

- Platform: https://github.com/maxgerhardt/platform-raspberrypi (core: earlephilhower)
- PIO I2S reference: https://github.com/malacalypse/rp2040_i2s_example
- No remote for this repo — do not create/push one unasked.

### Relevant docs

- `docs/I2S.md` — driver design + budget (clocks are COMPUTED, not measured)
- `docs/BUILD-NOTES.md` — per-phase verified evidence (read before touching DSP)
- `docs/PINOUT.md` — authoritative pin map; CD4051BE at 3.3 V; latching footswitch
- `BOM.csv` — what is bought/missing; no estimated prices allowed
- `host/ini_map.h` — the single source of truth for the 0-100 parameter scaling
- `.hermes/handoff/2026-11-01_194703-zuko-fase1-pecas-compradas.md` — bring-up order

---

## Quick resume prompt

Continuar o projeto Zuko (RP2350) em ~/Projects/zuko-rp2350. Leia primeiro
.hermes/handoff/2026-11-02_rt-jcm800-presets.md, depois rode: make test,
make firmware, make verify. Estado: toda a cadeia DSP (JCM800 analítico + tone
stack + cab sim) roda offline no harness e AO VIVO no Mac via Tank G
(make live), commits até 9110ed3, nada em silício ainda — peças do pedido
2026-10-31 não chegaram. Próximos passos: (1) bring-up quando as peças
chegarem, na ordem do handoff de 2026-11-01; (2) perguntar se o firmware
default passa a ser jcm800 e ligar no main.cpp; (3) não mexer no
host/presets/jcm800-punk.ini que está com ajustes meus sem commit.
