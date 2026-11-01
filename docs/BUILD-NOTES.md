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
