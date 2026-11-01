# I2S Driver — Design as Built (Phase 1)

## Overview

Full-duplex I2S at 48 kHz / 24-bit (32-bit slots), RP2350 as bus master.
PCM1808 (ADC) and PCM5102A (DAC) are slaves. Audio path: ADC -> PIO RX ->
DMA -> callback -> DMA -> PIO TX -> DAC. Pass-through only in this phase.

## PIO Architecture

**Choice: two state machines (output master + input slave) plus one for MCLK.**
Three SMs on PIO0, one SM spare.

The single-SM full-duplex option was rejected: it would need both `in` and
`out` instructions per bit-clock cycle plus side-set for BCLK/LRCK. With
2-bit side-set consuming 2 of the 5 delay/side-set bits, there is no room
for the delay cycles needed to interleave `in` and `out` at the correct
edges. The two-SM split is what the community reference (malacalypse/
rp2040_i2s_example) recommends as the robust path, and it allows each SM
to join its FIFO (TX-only or RX-only), doubling FIFO depth to 8 entries.

### Why MCLK uses a PIO SM, not `clock_gpio_init`

`clock_gpio_init` on RP2350 only supports GPIOs 13, 15, 21, 23, 24, 25
(the GPOUT-capable pins). GP19 is not in that set. Since the pinout is
authoritative and GP19 is committed to MCLK, a PIO state machine generates
the 12.288 MHz clock via simple pin toggle. This costs one SM but the
program is 2 instructions and the SM is otherwise idle.

### State machines

| SM | Program | PIO clock | Divider | Function |
|---|---|---|---|---|
| 0 | `pio_mclk` | 24.576 MHz | 10 | Toggle GP19 → 12.288 MHz MCLK |
| 1 | `pio_out_master` | 6.144 MHz | 40 | DOUT on GP23, BCLK/LRCK via 2-bit side-set on GP21/GP22 |
| 2 | `pio_in_slave` | 24.576 MHz | 10 | DIN on GP20, synchronized to BCLK/LRCK from SM1 |

All dividers are exact integers from clk_sys = 245.76 MHz. No fractional
division, no jitter from divider quantization.

### PIO programs

**MCLK** (2 instructions): `set pins 1` / `set pins 0`, free-running wrap.
One MCLK period = 2 PIO cycles = 2 × (1/24.576 MHz) = 81.38 ns → 12.288 MHz.

**Output master** (8 instructions): 2-bit side-set drives BCLK (GP21) and
LRCK (GP22). Each frame: `set x, 30` (loop counter for 31 data bits),
`pull noblock` (gets next word or recycles OSR), then 31× `out pins, 1` +
`jmp x--`. The `pull noblock` provides the I2S one-bit-clock delay after
the LRCK transition. Left frame uses side 0b0x, right frame uses side 0b1x.
FIFO joined TX (8-deep).

**Input slave** (18 instructions): no side-set, reads DIN on BCK rising
edges via `wait 1 pin 1` / `in pins, 1`. Pin mapping: in_base = GP20
(DIN), in_base+1 = GP21 (BCK), in_base+2 = GP22 (LRCK). JMP pin = GP22
(LRCK) for frame detection. Runs at 8× BCK to guarantee enough PIO cycles
between edges for the wait-in-jmp sequence. FIFO joined RX (8-deep).
Entry point at instruction 4 (`wait 0 pin 2`) to synchronize with the
L frame at startup.

All three SMs start simultaneously via `pio_enable_sm_mask_in_sync`.

## DMA — Ping-Pong Double Buffering

**Four DMA channels.** This is not a design choice but a requirement of
the ping-pong topology: each direction (TX and RX) needs a data channel
that moves samples and a control channel that reloads the buffer address.

| Channel | Direction | Role |
|---|---|---|
| TX data | Core → PIO TX FIFO | Reads from `tx_buf[N]`, paced by PIO TX DREQ |
| TX ctrl | (reload) | Writes next buffer address into TX data channel's read-address-trigger register |
| RX data | PIO RX FIFO → Core | Writes to `rx_buf[N]`, paced by PIO RX DREQ |
| RX ctrl | (reload) | Writes next buffer address into RX data channel's write-address-trigger register |

Each control channel reads from a 2-entry address table with ring wrapping
(ring size = 3, i.e. 8 bytes). When a data channel completes its transfer
of `I2S_BLOCK_SAMPLES` (64) words, it chains to its control channel, which
writes the next buffer pointer into the trigger alias register, restarting
the data channel on the alternate buffer.

The RX data channel fires `DMA_IRQ_0` on completion. The ISR determines
which buffer just filled, calls the audio callback with that RX buffer and
the corresponding TX buffer, and instruments the cycle count.

### Buffer layout

```
int32_t rx_buf[2][64];  // [ping/pong][L0,R0,L1,R1,...,L31,R31]
int32_t tx_buf[2][64];  // same layout
```

Each buffer is 64 × 4 = 256 bytes. Total DMA buffer SRAM: 1024 bytes.
Data format: 24-bit audio left-aligned in 32-bit words (bits [31:8]).

## Timing Budget

### Computed values (from clock tree, not measured)

| Quantity | Value | Derivation |
|---|---|---|
| Bit clocks per block | 2048 | 32 samples × 2 ch × 32 bits |
| Block period | 666.7 µs | 2048 / 3.072 MHz |
| CPU cycles per block | 163,840 | 666.7 µs × 245.76 MHz |
| Single-buffer latency | 0.667 ms | one block at 48 kHz |
| Double-buffer worst case | 1.333 ms | two blocks (< 2 ms target) |

### Instrumentation (built into firmware, not yet measured on hardware)

The DMA ISR reads `rp2040.getCycleCount()` before and after the audio
callback. Min, max and cumulative cycles are accumulated. Every ~100 blocks
(≈67 ms), `loop()` prints:

```
[budget] min=N avg=N max=N cycles  (avg X.X% max X.X% of 163840)
```

### What is NOT yet measured

All cycle counts above are **computed**. No hardware run has occurred.
The pass-through callback is a `memcpy` of 256 bytes; on Cortex-M33 at
245.76 MHz this should take well under 1000 cycles (< 0.6% of budget).
The actual number will come from the serial output on first hardware run.

### Build verification (2026-10-31)

The build was executed in Docker by the orchestrator after this design was written,
and the evidence is in [BUILD-NOTES.md](BUILD-NOTES.md):

| Checked | Result |
|---|---|
| `pio run -e rpipico2` | SUCCESS — RAM 1.9%, flash 1.0%, `firmware.uf2` = 106496 B |
| `__not_in_flash_func` actually placed in SRAM | `dma_irq_handler` @ `0x2000_0110`, `audio_passthrough` @ `0x2000_01a8` |
| PIO opcodes (hand-assembled, no `.pio` file) | all 28 words match `pioasm` output exactly |
| Entry point of the input program | index 4, matching `PIO_IN_SLAVE_ENTRY` |

What this does **not** cover: the clocks, the ping-pong buffer alternation and the audio
path itself. Those are the hardware tests below.

### Decision rule

If the pass-through handler exceeds 5% of the block budget (8192 cycles),
something is wrong with the DMA or PIO configuration (cache misses,
bus contention, interrupt latency). Investigate before adding DSP.

The full DSP chain (Phase 2–3: waveshaper + tone stack + 1024-tap FIR)
must stay under ~80% of the block budget (131,072 cycles) to leave
headroom for jitter and future features. If it exceeds that, either
the block size must increase or the FIR must move to fixed-point with
SIMD intrinsics.

## Hardware Acceptance Tests

1. **MCLK frequency**: measure GP19 with a frequency counter or scope.
   Must read 12.288 MHz ± 0.01%.
2. **BCLK frequency**: measure GP21. Must read 3.072 MHz.
3. **LRCK frequency**: measure GP22. Must read 48.000 kHz.
4. **Phase**: LRCK must transition one BCLK cycle before the first data
   bit (standard I2S timing). Verify with a logic analyzer on GP21+GP22+GP23.
5. **Audio pass-through**: feed a 1 kHz sine from a signal generator into
   the PCM1808, monitor the PCM5102A output on a scope. The output must be
   a clean sine at the same frequency with no dropouts.
6. **Budget report**: the serial output must show cycle counts, and the
   pass-through max must be well under 5% of 163,840.
7. **No underruns**: run for 10 minutes. The LED must keep blinking (core 0
   is not hung), and the budget report must keep printing.
