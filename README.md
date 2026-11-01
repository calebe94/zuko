# Zuko (RP2350)

Standalone digital guitar stompbox on a Raspberry Pi Pico 2 (RP2350A, dual
Cortex-M33 with hardware single-precision FPU). It models two British tube
preamps (Marshall 1959 Super Lead "Plexi" and JCM800 2203), a passive FMV
interactive tone stack, a power-amp presence filter, and a Marshall 1960B 4x12
cabinet via a 1024-tap direct-form FIR. 48 kHz, 24-bit audio. The preamp is
modeled analytically (tanh triode waveshapers with bias and cathode sag); no
neural networks.

## Status

Phase 0: repository skeleton and build configuration only. `src/main.cpp`
proves the toolchain, the clock and the dual-core split. There is no DSP, I2S,
or UI code yet.

The Phase 0 build is **verified**, not assumed: `pio run -e rpipico2` succeeds
inside the Docker image and produces `firmware.uf2` (RAM 1.7%, flash 0.9%). The
hardware FPU is confirmed in the linked ELF with `objdump`, and the CMSIS-DSP
`arm_fir_f32` symbols are confirmed present in the library that ships with the
platform. Raw evidence and the exact commands: [docs/BUILD-NOTES.md](docs/BUILD-NOTES.md).

## Architecture

- Toolchain: PlatformIO, platform `maxgerhardt/platform-raspberrypi`, framework
  `arduino`, core `earlephilhower`, board `rpipico2`.
- I2S: the RP2350 generates the whole clock tree (route B, decided): MCLK
  12.288 MHz on GP19, BCLK 3.072 MHz on GP21, LRCK 48 kHz on GP22, all divided
  exactly from a 245.76 MHz system clock. The PCM1808 ADC and the PCM5102A DAC
  are slaves. See [docs/PINOUT.md](docs/PINOUT.md).
- Pots: six pots through a 74HC4051 analog multiplexer into one ADC channel.
- Core 0: audio DSP only. Core 1: UI, control, coefficient calculation.
- Clock: 245.76 MHz at 1.20 V core, fallback 184.32 MHz (`board_build.f_cpu` in
  `platformio.ini`). Not 250 MHz: the I2S clock tree needs exact integer
  divisions of the system clock.
- Compiler flags: the brief's `-mfloat-abi=hard -mfpu=fpv5-sp-d16` do **not**
  work with this platform and were removed. `-ffast-math` is deferred until its
  effect on `tanhf()` is measured. Both facts are in `platformio.ini`.

## Build

With PlatformIO installed:

```sh
pio run -e rpipico2
```

With Docker:

```sh
docker build -t zuko-rp2350-build .
docker run --rm -v "$PWD":/workspace zuko-rp2350-build
```

The UF2 ends up in `.pio/build/rpipico2/firmware.uf2`. Hold BOOTSEL while
plugging in the Pico 2 and copy it to the `RP2350` drive.

Note: the Docker image installs the PlatformIO platform but not the toolchain
for the Arduino core, which PlatformIO fetches on the first build (~200 MB).
The CI workflow caches `~/.platformio`, so only the very first run is slow.

## Phase 0 serial output

Open the USB serial monitor at 115200 (`pio device monitor`). The banner shows
the configured `F_CPU`, the measured system clock, the core voltage, a `tanhf`
waveshaper and a 32-tap FIR with cycles/MAC (a low single-digit figure means the
FPU path is real). Core 1 prints a heartbeat every 500 ms.

## Layout

| Path | Purpose |
|---|---|
| `platformio.ini` | build configuration |
| `src/main.cpp` | Phase 0 skeleton |
| `docs/README.md` | index of the project documentation |
| `docs/PINOUT.md` | pin map and hardware rationale |
| `docs/BUILD-NOTES.md` | verified build evidence and toolchain findings |
| `scripts/` | host-side tools (placeholder) |
| `Dockerfile` | reproducible build environment |
| `.github/workflows/build.yml` | CI build and firmware artifact |
