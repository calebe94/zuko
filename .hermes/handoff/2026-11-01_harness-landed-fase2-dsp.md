# Handoff — Zuko (RP2350) — host harness landed, Phase 2 DSP starting

**Date:** 2026-11-01 (-03)
**Repo:** `~/Projects/zuko-rp2350`, branch `main`, no remote. Head: `38724d9`.

## State

- Phase 1: build-verified (see `2026-11-01_194703-*` handoff for the full verification
  block). Nothing has run on silicon; parts ordered 2026-10-31, not delivered.
- NEW: host-native DSP harness landed and verified:
  - `src/dsp/dsp.h` — hardware-independent DSP core (`waveshape`, `fir_at`,
    `process_block`). No `#ifdef`; both the Arduino cross-build and the Mac host
    build compile the same header.
  - `host/` — `harness.cpp` (WAV in/out + `--selftest`), `Makefile`, `test.py`.
  - Verified: `make -C host test` → SELFTEST PASS + pass-through bit-exact over
    48000 stereo frames. Docker `pio run -e rpipico2` → SUCCESS, RAM 1.9%
    (9960 B), flash 1.0% (40904 B) — unchanged from the Phase 1 numbers.
- BOM: U6 = LD1117-5V stopgap (`buy`, R$ 2.52, RC 10R+470uF note, Curitiba sources
  RL Eletronica / Betatronic); U6a = TPS7A4700 ready-made module (`decide`, buy ONLY
  if the measured noise test shows the 1117 rail is audible).

## Verification commands

```bash
cd ~/Projects/zuko-rp2350
make -C host test   # expect: SELFTEST PASS + bit-exact pass-through
docker run --rm -v zuko-pio-cache:/root/.platformio -v "$PWD":/workspace \
  -w /workspace zuko-rp2350-build pio run -e rpipico2   # expect SUCCESS
```

## Next steps (in order)

1. **Phase 2 DSP, host-first:** first preamp model in `src/dsp/` (waveshaper with
   real transfer curves), validated through `host/` against a double-precision
   reference BEFORE any firmware change. Firmware only changes after the host test
   passes and the Docker build still succeeds.
2. Parts arrival → hardware bring-up in the order given by
   `2026-11-01_194703-zuko-fase1-pecas-compradas.md` (rails → scope GP19/21/22 →
   `[budget]` lines → 1 kHz sine pass-through). Requires the USER (parts in hand).
3. U5 (TL074/MC33079) + U6 (1117) purchase: USER action, does not block DSP work.
4. Then replace the "not yet measured" section of `docs/I2S.md` with real `[budget]`
   numbers once silicon runs.

## Rules that stay in force

- Never propose NAM in this project. Analytical modeling only.
- Comments/code in English; conversation with the user in pt-BR.
- No price is estimated in BOM.csv — quoted/paid sources only.
- User confirms before irreversible actions; commits are fine to make during the
  loop (user explicitly authorized a commit-per-plan execution loop on 2026-11-01).
- Social-media track (DEFERRED by the user, 2026-11-01): he wants to post a real-time
  build log (every milestone: commit, test, part arriving) on "anti-social" networks.
  Platform choice and account/API setup are explicitly NOT to be worked on now — revisit
  only when he brings it up.
