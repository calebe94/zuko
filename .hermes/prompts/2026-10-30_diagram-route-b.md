# Task: update the Excalidraw diagram — the I2S clock decision is now CLOSED (route B)

You previously generated `/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).excalidraw`
in this vault. The user has now DECIDED the clock question, so the drawing is out of
date and partly WRONG: it shows the PCM1808 as the bus master and the clocks flowing
from it into the RP2350. That is no longer the design.

**Decision made: route B.** The RP2350 (Pico 2 / RP2350A) generates the whole clock
tree itself, and the converters are slaves.

- `clk_sys = 245.76 MHz` (chosen because every division below is an exact integer)
- `MCLK = clk_sys / 20 = 12.288 MHz` -> PCM1808 `SCK` pin
- `BCLK = clk_sys / 40 = 3.072 MHz` (state machines emit one bit per two clocks)
- `LRCK = 48 kHz`, produced by counting 32 bits per channel in the same state machine
- PCM1808 is in **slave mode** (MD0 = MD1 = 0, which is the chip's own power-on default,
  because those pins have internal 50 kOhm pull-downs; FMT = 0 already means I2S 24-bit)
- The PCM5102A DAC is a slave on the same BCLK/LRCK lines
- Bonus that made route B safe: at this same clk_sys, 96 kHz is also exact
  (MCLK = clk_sys/10, BCLK = clk_sys/40)

## What to change in the drawing

1. **Add a new pin and a new arrow.** The RP2350 gets a `MCLK` output on **GP19**, and a
   new arrow goes RP2350 -> PCM1808 labelled `GP19 -> SCK (MCLK 12,288 MHz)`.
2. **Reverse the direction of the clock arrows.** The arrows that currently run
   PCM1808 -> RP2350 labelled `BCK -> GP21` and `LRCK -> GP22` must now be
   RP2350 -> PCM1808, labelled `GP21 -> BCK (3,072 MHz)` and `GP22 -> LRCK (48 kHz)`.
   Keep the data arrow PCM1808 -> RP2350 as it is: `DOUT -> GP20 (I2S DIN)`.
   Keep the DAC as it is: `GP23 -> DIN (I2S DOUT)`, with BCK/LRCK shared on the same lines.
3. **Fix the ADC box title.** It currently says `PCM1808 — ADC 24-bit I2S`. It must now
   say the ADC is a **slave**: `PCM1808 — ADC 24-bit I2S (escravo)`.
4. **Replace the "open decision" card** (`Decisão aberta — clock do PCM1808`) with a
   closed-decision card titled `Clock I2S — rota escolhida: RP2350 master`, containing:
   - `clk_sys = 245,76 MHz` (divisões inteiras exatas, MCLK = clk_sys/20)
   - `MCLK 12,288 MHz -> GP19 -> SCK do PCM1808`
   - `BCLK 3,072 MHz -> GP21` e `LRCK 48 kHz -> GP22`
   - `PCM1808 em modo escravo (MD0 = MD1 = 0, padrão de fábrica do chip)`
   - `DAC PCM5102A escravo nos mesmos BCLK/LRCK`
   - `96 kHz também sai exato neste clk_sys (MCLK = clk_sys/10)`
   - one line about the rejected alternative: `Rota A (oscilador de 12,288 MHz dedicado) descartada: custa ~R$20 e exige o PIO em modo escravo`
5. Update the pin block inside the RP2350 box if it lists BCLK/LRCK as inputs; they are
   outputs now. `I2S DIN GP20` stays an input.
6. Any remaining label that says the PCM1808 is master, or that the RP2350 is a slave,
   must be reversed or removed.

Keep everything else exactly as it is: the pots, the 74HC4051, the OLED, the footswitch,
the LED, the power section, the front end, the legend and the other two cards
(`Cadeia de DSP`, `Fase 0 — build verificado`).

## How to do it

Prefer editing the generator script and re-running it, rather than patching JSON:
look for `/tmp/plexi_work/gen_plexi.py` (it may have been cleaned up). If it is gone,
write a new script under `/tmp` that reproduces the current layout and applies the
changes above. Follow the skill
`/Users/calebe94/.hermes/skills/note-taking/excalidraw-obsidian-integration/SKILL.md`
(element schema, zero-padded `index`, the `0.62 x fontSize` text budget) and use its
`scripts/preview_excalidraw.py` to rasterize the result for inspection.

Validate before reporting:
- `json.load` on the written file, unique ids, zero-padded sorted indexes
- no text overflowing its box, no dangling arrows, nothing off-canvas
- rasterize and actually look at the image if you can read images; if you cannot, say so
- confirm the words `simplesmente`, `obviamente` and `é só` do not appear

## Hard rules

- Write ONLY `/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).excalidraw`.
  Do not touch the `.md` next to it (it is being edited concurrently by another process),
  do not touch any other vault file.
- No git command that changes state. No commit. No branch.
- Labels in Portuguese; part numbers, pin names and technical terms keep their original form.
- Do not invent hardware facts. Everything you need is in this prompt.

Report: the element count and type histogram, the number of arrows before and after,
the exact validation commands with real output, and anything you could not verify.
