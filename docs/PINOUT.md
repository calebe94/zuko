# Zuko (RP2350) — Pinout (Raspberry Pi Pico 2, RP2350A)

This table is authoritative. Firmware and wiring must match it exactly.

| Function | GPIO | Notes |
|---|---|---|
| I2S MCLK output (to PCM1808 SCK) | GP19 | 12.288 MHz = clk_sys / 20; any pin works |
| I2S BCLK output (to PCM1808 BCK and DAC) | GP21 | 3.072 MHz = clk_sys / 40; must be adjacent to GP22 |
| I2S LRCK output (to PCM1808 LRCK and DAC) | GP22 | 48 kHz, counted from 32 bits per channel |
| I2S DIN input (from PCM1808 DOUT) | GP20 | any pin works |
| I2S DOUT output (to PCM5102A DIN) | GP23 | any pin works |
| Pot mux common output (analog in) | GP26 | ADC0, 100 nF to AGND |
| Pot mux select S0 / S1 / S2 | GP6 / GP7 / GP8 | outputs |
| Footswitch (channel toggle) | GP14 | to GND, INPUT_PULLUP, 20 ms debounce |
| Status LED | GP15 | external LED, active high |
| OLED I2C0 SDA / SCL | GP4 / GP5 | SSD1306 128x64 at 0x3C |
| Debug UART0 TX / RX | GP0 / GP1 | 115200 |
| Spare analog (future expression pedal) | GP27 | ADC1, unused for now |

## Why the I2S pins sit where they do

- **GP21/GP22 (BCLK + LRCK) must be two consecutive pins** because the PIO drives
  them with a single 2-bit side-set, which always addresses an adjacent pair. They
  can be in either order; `swapClocks()` selects which.
- **GP20 (DIN) and GP23 (DOUT) can be any pin.** They are the PIO IN base and OUT
  base, addressed one bit at a time.
- They moved off the original GP26/27/28 so that all four ADC-capable pins
  (GP26-GP29) stay free.

## I2S clocking: DECIDED — route B, the RP2350 generates the clock tree

The PCM1808 always needs a 12.288 MHz system clock on its SCK pin (256 x 48 kHz),
in master and slave mode alike. The cheap breakout module sold in Brazil and on
AliExpress does **not** carry a crystal: its SCK pin is an input, so something
else has to generate that clock. (TI's own evaluation board does have the
crystal; the module does not.)

**Decision: the RP2350 generates everything; the converters are slaves.**

| Signal | Division | Frequency | Goes to |
|---|---|---|---|
| `clk_sys` | base | 245.76 MHz | the chosen base, every division below is an integer |
| `MCLK` | clk_sys / 20 | 12.288 MHz | GP19 -> PCM1808 `SCK` |
| `BCLK` | clk_sys / 40 | 3.072 MHz | GP21 -> PCM1808 `BCK` and DAC `BCK` |
| `LRCK` | count 32 bits per channel | 48 kHz | GP22 -> PCM1808 `LRCK` and DAC `LRCK` |

- The PCM1808 is in **slave mode at its own power-on default**: `MD0` and `MD1`
  have internal 50 kΩ pull-downs (both low = slave, SCKI auto-detect at 256 fS)
  and `FMT` low already means I2S 24-bit. **No strap has to be soldered.** Verify
  it on the board you buy anyway, and note that a module hard-strapped to master
  mode would have to be modified.
- 96 kHz is also exact at this same base: `MCLK` = clk_sys / 10 = 24.576 MHz and
  the bit-clock state machine = clk_sys / 20 = 6.144 MHz. The CPU budget, not the
  clock, is what would limit 96 kHz.
- The PCM5102A is a slave on the same BCLK/LRCK. Its `SCK` can either stay tied
  to GND (internal PLL derives the system clock from BCLK) or be fed the same
  12.288 MHz MCLK — feeding MCLK is better if the module exposes the pad.

### Rejected route A, kept on record

| Route | 12.288 MHz comes from | PCM1808 role | Why it lost |
|---|---|---|---|
| A | a dedicated active oscillator (~R$20) | bus master; it drives BCLK and LRCK | crystal-grade jitter, but it forces the PIO to run as a **slave** on external clocks — the hard, thinly documented path, and this project already carries the risk of the DSP work |
| **B (chosen)** | the RP2350 | slave | R$0 and the PIO stays in the well-trodden master mode; the clock comes from the RP2350 PLL, so slightly more jitter |

Route A is not burned off: both routes feed the **same node** (SCK). If a noise
measurement later shows the PLL clock is the problem, the migration is one
jumper, one oscillator, and flipping the PIO to slave mode.

**Acceptance test carried into bring-up:** measure the ADC noise floor with the
clock from the PLL, then with an external oscillator on the same node. That is
what turns "expected to be inaudible" into a number.

### Phase 1 implementation note (not yet built)

Both options below work with this pinout:
1. One state machine in full duplex: `out pins, 1` for DOUT, `in pins, 1` for DIN,
   side-set for BCLK/LRCK. Fewest resources.
2. Two state machines: an output master that generates BCLK/LRCK by side-set, and
   an input machine waiting on those same two pins. This is the configuration the
   community reference (rp2040_i2s_example) recommends as most robust.

## Pot multiplexer

Six potentiometers are read through one 74HC4051 (8:1 analog mux) into a single
ADC channel (GP26 / ADC0), with S0/S1/S2 on GP6/GP7/GP8.

| Mux channel | Potentiometer |
|---|---|
| Y0 | Gain |
| Y1 | Bass |
| Y2 | Middle |
| Y3 | Treble |
| Y4 | Presence |
| Y5 | Master |
| Y6, Y7 | unused (tie the inputs to AGND) |

- Four ADC-capable pins exist on this part (GP26-GP29 = ADC0-ADC3) and all four are
  free once I2S moves to GP19-GP23. Six pots would still need six channels, which
  this chip does not have, so the pots share one ADC channel through the mux.
- One shared ADC input keeps pot-to-pot gain and offset identical.
- 100 nF from the mux output to AGND filters the switching noise and gives the
  ADC sample-and-hold a low-impedance source.
- GP27 (ADC1) is left free for a future expression pedal.

## Never enable the internal pull-down resistors

**Do not call `pinMode(pin, INPUT_PULLDOWN)` or enable a pull-down on any RP2350
pin.** The RP2350 has a documented silicon erratum: with the internal pull-down
enabled, the pin latches low after the first read and does not recover. Use the internal pull-**up**
(e.g. the footswitch on GP14 uses `INPUT_PULLUP` to GND) or an external resistor.

## Power rails

- The analog supply (PCM1808, PCM5102A analog side, 74HC4051 and the pot
  reference) must be kept **separate from the Pico's switching regulator
  output**. Do not feed it from 3V3(OUT) or VSYS directly.
- Use a dedicated low-noise LDO for the analog rail, fed from the same input
  supply, and join the analog and digital grounds at a single point.
- The Pico's on-board buck regulator switches at a frequency and with ripple
  that is audible as whine or hiss in a guitar signal chain if shared.
- Pot reference and ADC reference: use the analog rail, and decouple with
  100 nF at the mux plus bulk capacitance at the LDO output.
- PCM1808 full scale is 3 Vpp centred on VREF = VCC/2 = **2.5 V**, so the input
  buffer must bias the guitar signal at 2.5 V, not at 1.65 V. A 1.65 V bias (copied
  from a 3.3 V design) clips half the waveform.
