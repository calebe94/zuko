# Zuko (RP2350)

Standalone digital guitar stompbox on a Raspberry Pi Pico 2 (RP2350A, dual
Cortex-M33 with hardware single-precision FPU). It models two British tube
preamps (Marshall 1959 Super Lead "Plexi" and JCM800 2203), a passive FMV
interactive tone stack, a power-amp presence filter, and a Marshall 1960B 4x12
cabinet via a 1024-tap direct-form FIR. 48 kHz, 24-bit audio. The preamp is
modeled analytically (tanh triode waveshapers with bias and cathode sag); no
neural networks.

## Status

**Phase 1: I2S driver with PIO master clocking and DMA ping-pong.**

The I2S audio path is implemented: three PIO state machines on PIO0 generate
MCLK (12.288 MHz), BCLK (3.072 MHz) and LRCK (48 kHz), with DMA ping-pong
double buffering over 32-sample blocks. The audio callback is a pass-through
(copy ADC input to DAC output). Cycle-budget instrumentation reports min/avg/max
handler cost over Serial.

The Phase 1 build is **verified**, not assumed: `pio run -e rpipico2` succeeds inside
the Docker image and produces `firmware.uf2` (106496 B, RAM 1.9%, flash 1.0%), the
hardware FPU is confirmed in the linked ELF, and both the DMA ISR and the audio
callback are confirmed to sit in SRAM (`0x2000_0110` and `0x2000_01a8`). The
hand-assembled PIO opcodes were cross-checked against `pioasm` and matched. Raw
evidence and the exact commands: [docs/BUILD-NOTES.md](docs/BUILD-NOTES.md).

What is still unverified is anything that needs a board: I2S timing, the DMA
ping-pong actually alternating buffers, and the pass-through itself. The firmware
carries the instrumentation for the block-processing budget, but no cycle figure has
been measured on silicon yet.

## Architecture

- Toolchain: PlatformIO, platform `maxgerhardt/platform-raspberrypi`, framework
  `arduino`, core `earlephilhower`, board `rpipico2`.
- I2S: the RP2350 generates the whole clock tree (route B, decided): MCLK
  12.288 MHz on GP19, BCLK 3.072 MHz on GP21, LRCK 48 kHz on GP22, all divided
  exactly from a 245.76 MHz system clock. The PCM1808 ADC and the PCM5102A DAC
  are slaves. See [docs/PINOUT.md](docs/PINOUT.md).
- PIO: 3 state machines on PIO0 (MCLK toggle, output master with side-set
  clocks, input slave synchronized to those clocks). See [docs/I2S.md](docs/I2S.md).
- DMA: 4 channels (TX data + TX ctrl, RX data + RX ctrl) for ping-pong
  double buffering. Block size: 32 samples/channel, total latency < 1.4 ms.
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

## Serial output

Open the USB serial monitor at 115200 (`pio device monitor`). Phase 1 prints:

1. The Phase 0 banner (clock, voltage, FPU sanity).
2. I2S configuration (sample rate, bit depth, block size).
3. "I2S started — pass-through active."
4. Periodic budget reports: `[budget] min=N avg=N max=N cycles (avg X% max X% of 163840)`.
5. Core 1 heartbeat every 500 ms.

## Layout

| Path | Purpose |
|---|---|
| `platformio.ini` | build configuration |
| `src/main.cpp` | Phase 1: audio pass-through + budget reporting |
| `src/i2s.h` | I2S driver public interface |
| `src/i2s.cpp` | PIO programs + DMA ping-pong engine |
| `docs/README.md` | index of the project documentation |
| `docs/PINOUT.md` | pin map and hardware rationale |
| `docs/I2S.md` | I2S driver design as built |
| `docs/BUILD-NOTES.md` | verified build evidence and toolchain findings |
| `scripts/` | host-side tools (placeholder) |
| `Dockerfile` | reproducible build environment |
| `.github/workflows/build.yml` | CI build and firmware artifact |
