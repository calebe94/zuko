# Task: REPLACE the diagram file with a real component-connection diagram

You are working in the user's Obsidian vault. Vault text is Brazilian Portuguese.
Read everything you need first; invent nothing.

## The problem you are fixing

The file `/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).excalidraw` exists
and contains a **specification board**: 15 cards and 134 text lines (power rails,
DSP chain, clock decision, pin map, build status, shopping checklist) arranged in
three columns, with only **3 arrows**. The user asked for something different: a
diagram where **the components are boxes and the connections between them are
arrows**. The signal path currently exists only as prose inside cards
("DOUT -> GP20", "BCK -> GP21").

**Your job: rewrite that same file so the component connection diagram is the
main content.** Keep the same file path and the same base name, because the note
`/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).md` already embeds it with
`![[Plexi Box (RP2350).excalidraw|9999]]`.

## Required content of the NEW diagram

A connection topology, laid out left to right in signal order, with components as
boxes and labelled arrows between them.

Boxes (each with a visible title):
- `Jack P10 (IN) — Guitarra`
- `Front-End Analógico` — inside it, as sub-labels: buffer JFET de alta impedância (Rin >= 1 MΩ), filtro Sallen-Key 2ª ordem Butterworth (fc ~ 22 kHz), clamp BAT54S, bias 2,5 V (= VCC/2 do PCM1808)
- `PCM1808 — ADC 24-bit I2S`
- `RP2350A (Pico 2)` — with two inner boxes: `Core 0 — DSP` and `Core 1 — UI/controle`
- `PCM5102A — DAC 24-bit I2S`
- `Jack P10 (OUT) — Line out`
- `74HC4051 — Mux 8:1 analógico`
- `6x Potenciômetros B10K` — Gain, Bass, Middle, Treble, Presence, Master
- `SSD1306 OLED 0.96" 128x64`
- `Footswitch SPST (troca de canal)`
- `LED de status`
- `Alimentação` — 9 V DC center-negative -> LDO 5 V analógico, LDO 3,3 V digital, terra analógico/digital separados (star ground)

Arrows, each with a text label next to it:
- Guitarra -> Front-End (sinal analógico, ~0,1 a 1 V)
- Front-End -> PCM1808 (VINL)
- PCM1808 -> RP2350: three separate labelled arrows — `DOUT -> GP20 (I2S DIN)`, `BCK -> GP21 (3,072 MHz)`, `LRCK -> GP22 (48 kHz)`
- RP2350 -> PCM5102A: `GP23 -> DIN (I2S DOUT)`; plus `BCK`/`LRCK` shared from the same clock lines (draw them as the same two lines branching, or label the DAC box "BCK/LRCK compartilhados")
- PCM5102A -> Jack OUT (VOUTL/VOUTR -> buffer -> jack)
- Pots -> Mux (6 wipers) ; Mux -> RP2350: `saída comum -> GP26 (ADC0)`; RP2350 -> Mux: `GP6 / GP7 / GP8 (S0/S1/S2)`
- RP2350 <-> OLED: `I2C0 — SDA GP4 / SCL GP5`
- Footswitch -> RP2350: `GP14 (INPUT_PULLUP)`
- RP2350 -> LED: `GP15`
- Alimentação -> the boxes that need power (label the rails: 5 V analógico, 3,3 V digital)

Then, BELOW or to the right of the topology, keep at most THREE compact cards
with the state of the project (do not keep the whole old board):
1. `Cadeia de DSP (Core 0, 48 kHz)` — DC blocker -> triode waveshaper 2x oversampled (Plexi 1959 / JCM800 2203) -> tone stack FMV 3ª ordem (Yeh-Smith, TDF-II) -> presence biquad 3,5 kHz -> FIR 1024 taps (CMSIS-DSP arm_fir_f32, Marshall 1960B)
2. `Decisão aberta — clock do PCM1808` — the module has NO crystal and needs 12,288 MHz on SCK; Rota A = active oscillator, ADC is bus master, the PIO is a slave (~R$20, best jitter); Rota B = the RP2350 generates MCLK/BCLK/LRCK from clk_sys 245,76 MHz and stays in PIO master mode (R$0)
3. `Fase 0 — build verificado (30-10-2026)` — `pio run -e rpipico2` succeeded in 147,80 s; RAM 1,7%; flash 0,9%; `firmware.uf2` generated; `-mfloat-abi=hard` removed after a real failure

## Procedure — follow the skill, do not improvise the schema

Read these before generating anything:
- `/Users/calebe94/.hermes/skills/note-taking/excalidraw-obsidian-integration/SKILL.md` — element schema, the zero-padded `index` rule, the text-overflow budget, the card helper.
- `/Users/calebe94/.hermes/skills/note-taking/excalidraw-obsidian-integration/templates/excalidraw_board_card_helper.py` — drop-in helpers, including the `card()` helper and a `fits()` text-width check.
- `/Users/calebe94/.hermes/skills/note-taking/excalidraw-obsidian-integration/scripts/preview_excalidraw.py` — the rasterizer for the validation loop.
- `/Users/calebe94/Projects/plexi-box-rp2350/docs/PINOUT.md` — the authoritative pin map. All GPIO numbers must come from here.
- The current `/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).excalidraw` — read it, reuse whatever is still true, and be aware it is the file you are replacing.

Generate the file with a Python script (never hand-written JSON). Every element
needs a unique `id`, a zero-padded `index`, `seed`, `version`, `versionNonce`,
`boundElements`, `frameId`, `hasTextLink`; text elements additionally need
`text`/`rawText`/`originalText`, `fontSize`, `fontFamily`, `textAlign`,
`verticalAlign: "top"`, `containerId: null`, `autoResize: true`, `lineHeight: 1.25`.

### Mandatory validation before you report

1. Assert `len(ids) == len(set(ids))` and print the element type histogram.
2. Rasterize with the preview script and inspect the PNG. If you can read images,
   look specifically for: text overflowing its box, arrows pointing at nothing,
   overlapping boxes, elements outside the canvas. Fix and re-render until clean.
   If you cannot read images, say so plainly and rely on the `fits()` arithmetic,
   and report that the visual check was NOT performed.
3. Confirm the file is valid JSON and that it still parses with
   `json.load` after writing.

## Hard rules

- Write ONLY `/Users/calebe94/Notas/1 - Projects/Plexi Box (RP2350).excalidraw`.
  Do not touch the `.md` note next to it, do not create or modify any other vault
  file, and do not touch `/Users/calebe94/Projects/plexi-box-rp2350`.
- The vault is a git repository with uncommitted changes: do NOT run any git
  command that changes state, do not commit, do not branch.
- Labels in Portuguese. Component part numbers, pin names and technical terms stay
  in their original form (PCM1808, SSD1306, GP20, I2S).
- NEVER use the words `simplesmente`, `obviamente` or `é só`.
- ASCII art panels, if you use any, need `fontFamily: 3`; avoid glyphs the
  rasterizer lacks.
- Report at the end: the absolute path, the element count and type histogram, the
  number of arrows, the exact validation commands you ran with their real output,
  and anything you could NOT verify.
