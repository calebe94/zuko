#!/usr/bin/env python3
"""Zuko (RP2350) - component connection diagram generator.
Writes the .excalidraw to argv[1]. Pin numbers come from docs/PINOUT.md."""
import json
import os
import random
import sys

OUT = sys.argv[1]
random.seed(2350)  # reproducible seeds

elements = []
_n = [0]
PX = 0.62  # px per char per fontSize (skill budget)

# palette (dark board)
BG = "#121212"
PANEL = "#1a1a1a"
INNER = "#242424"
TXT = "#e8e8e8"
SUB = "#b4bac1"
C_ANALOG = "#f08c00"
C_DIGITAL = "#4dabf7"
C_UI = "#9775fa"
C_JACK = "#adb5bd"
C_PWR = "#ff6b6b"
C_I2S = "#22b8cf"
C_CTRL = "#51cf66"
C_V33 = "#fcc419"
C_CARD = "#2f9e44"


def _base(etype, x, y, w, h, stroke, bg="transparent", sw=2, dashed=False):
    _n[0] += 1
    return {
        "id": "pb%03d" % _n[0],
        "type": etype,
        "x": x, "y": y, "width": w, "height": h, "angle": 0,
        "strokeColor": stroke,
        "backgroundColor": bg,
        "fillStyle": "solid",
        "strokeWidth": sw,
        "strokeStyle": "dashed" if dashed else "solid",
        "roughness": 0,
        "opacity": 100,
        "groupIds": [], "frameId": None,
        "roundness": None,
        "seed": random.randint(1, 2 ** 31 - 1),
        "version": 1,
        "versionNonce": random.randint(1, 2 ** 31 - 1),
        "isDeleted": False, "boundElements": [],
        "updated": 1790816521249, "locked": False,
        "index": "a%03d" % _n[0], "link": None, "hasTextLink": False,
    }


def rect(x, y, w, h, stroke, bg=PANEL, sw=2, rounded=True, dashed=False):
    el = _base("rectangle", x, y, w, h, stroke, bg, sw, dashed)
    if rounded:
        el["roundness"] = {"type": 3}
    elements.append(el)
    return el


def dot(x, y, color, r=5):
    el = _base("ellipse", x - r, y - r, 2 * r, 2 * r, color, color, 1)
    el["roundness"] = {"type": 2}
    elements.append(el)
    return el


def text(s, x, y, size, color, maxw=None, align="left"):
    """maxw = horizontal room the string must fit in (asserted)."""
    lines = s.split("\n")
    w = max(len(l) for l in lines) * size * PX
    if maxw is not None:
        assert w <= maxw, "TEXT OVERFLOW (%d > %d): %r" % (w, maxw, s)
    h = len(lines) * size * 1.25
    el = _base("text", x, y, int(w) + 2, int(h) + 1, color, sw=1)
    el.update({
        "text": s, "rawText": s, "originalText": s,
        "fontSize": size, "fontFamily": 1,
        "textAlign": align, "verticalAlign": "top", "containerId": None,
        "autoResize": True, "lineHeight": 1.25,
    })
    elements.append(el)
    return el


def box(x, y, w, h, title, lines, stroke, tsize=12, lsize=10, bg=PANEL, pad=14,
        title_h=None, sw=2, line_color=SUB):
    """Component box: rounded rect + title + optional body lines. Text widths asserted."""
    b = rect(x, y, w, h, stroke, bg, sw)
    inner = w - 2 * pad
    ty = y + 10 if lines else y + (h - tsize * 1.25) / 2
    text(title, x + pad, ty, tsize, TXT, maxw=inner)
    if lines:
        ly = ty + (title_h or (tsize * 1.25 + 8))
        text("\n".join(lines), x + pad, ly, lsize, line_color, maxw=inner)
    return b


def _bind(arrow, key, shape, gap=3):
    if shape is None:
        return
    arrow[key] = {"elementId": shape["id"], "focus": 0, "gap": gap}
    shape["boundElements"].append({"id": arrow["id"], "type": "arrow"})


def arrow(pts, color, start=None, end=None, both=False, dashed=False, sw=2, head=True):
    """pts = absolute polyline. start/end = shape the endpoint is bound to."""
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    el = _base("arrow" if head else "line", pts[0][0], pts[0][1],
               max(xs) - min(xs), max(ys) - min(ys), color, sw=sw, dashed=dashed)
    el["points"] = [[p[0] - pts[0][0], p[1] - pts[0][1]] for p in pts]
    el["lastCommittedPoint"] = None
    el["startBinding"] = None
    el["endBinding"] = None
    el["startArrowhead"] = "arrow" if (both and head) else None
    el["endArrowhead"] = "arrow" if head else None
    if head:
        el["elbowed"] = False
    _bind(el, "startBinding", start)
    _bind(el, "endBinding", end)
    elements.append(el)
    return el


def label(s, x, y, color, size=10, maxw=None):
    return text(s, x, y, size, color, maxw=maxw)


# ===================================================================== layout
MAIN_Y = 460  # signal axis

# ---- title (top-left)
text("Zuko (RP2350) — Diagrama de conexões", 0, 62, 18, TXT)
text("Sinal da esquerda para a direita. Caixas = componentes, setas = conexões.",
     0, 96, 11, SUB, maxw=520)

# ---- Jack IN
jack_in = box(0, 425, 230, 70, "Jack P10 (IN) — Guitarra", [], C_JACK)

# ---- Front-End Analogico
fe_x, fe_y, fe_w, fe_h = 370, 335, 350, 250
fe = rect(fe_x, fe_y, fe_w, fe_h, C_ANALOG, PANEL, 2)
text("Front-End Analógico", fe_x + 14, fe_y + 10, 13, TXT, maxw=fe_w - 28)
subs = [
    (["buffer JFET de alta impedância", "(Rin >= 1 MΩ)"], 50),
    (["filtro Sallen-Key 2ª ordem Butterworth", "(fc ~ 22 kHz)"], 50),
    (["clamp BAT54S"], 34),
    (["bias 2,5 V (= VCC/2 do PCM1808)"], 34),
]
sy = fe_y + 42
for ls, h in subs:
    rect(fe_x + 14, sy, fe_w - 28, h, C_ANALOG, INNER, 1)
    text("\n".join(ls), fe_x + 24, sy + (h - len(ls) * 11 * 1.25) / 2, 11, SUB,
         maxw=fe_w - 28 - 20)
    sy += h + 8

# ---- PCM1808
pcm_x, pcm_y, pcm_w, pcm_h = 870, 340, 290, 180  # widened 30 px to the left for the longer title
pcm = box(pcm_x, pcm_y, pcm_w, pcm_h, "PCM1808 — ADC 24-bit I2S (escravo)",
          ["módulo sem cristal", "SCK recebe 12,288 MHz do RP2350",
           "(ver decisão fechada abaixo)"], C_ANALOG)

# ---- RP2350
rp_x, rp_y, rp_w, rp_h = 1390, 330, 600, 220
rp = rect(rp_x, rp_y, rp_w, rp_h, C_DIGITAL, PANEL, 3)
text("RP2350A (Pico 2)", rp_x + 16, rp_y + 12, 14, TXT, maxw=300)
core0 = box(rp_x + 20, rp_y + 48, 270, 150, "Core 0 — DSP",
            ["DSP 100% em SRAM (__not_in_flash)", "I2S PIO + DMA ping-pong",
             "48 kHz / 24-bit"], C_DIGITAL, bg=INNER)
core1 = box(rp_x + 310, rp_y + 48, 270, 150, "Core 1 — UI/controle",
            ["leitura dos pots (mux + ADC0)", "OLED, footswitch e LED",
             "double-buffer lock-free"], C_UI, bg=INNER)

# ---- DAC
dac_x, dac_y, dac_w, dac_h = 2190, 400, 260, 160
dac = box(dac_x, dac_y, dac_w, dac_h, "PCM5102A — DAC 24-bit I2S",
          ["BCK/LRCK compartilhados", "slave: SCK em GND (PLL interna)",
           "VOUTL/VOUTR -> buffer"], C_ANALOG)

# ---- Jack OUT
jack_out = box(2650, 445, 230, 70, "Jack P10 (OUT) — Line out", [], C_JACK)

# ---- top row: pots, mux, oled, footswitch
pots = box(600, 60, 280, 140, "6x Potenciômetros B10K",
           ["Gain, Bass, Middle,", "Treble, Presence, Master"], C_UI)
mux = box(1000, 60, 260, 140, "74HC4051 — Mux 8:1 analógico",
          ["S0/S1/S2 escolhem o pot", "saída comum com 100 nF p/ AGND"], C_ANALOG)
oled = box(1560, 60, 230, 100, "SSD1306 OLED 0.96\" 128x64", ["I2C0, endereço 0x3C"], C_UI)
fs = box(1820, 60, 250, 70, "Footswitch SPST (troca de canal)", [], C_UI, tsize=11)

# ---- LED
led = box(1800, 650, 190, 50, "LED de status", [], C_UI)

# ---- Alimentacao
pw_x, pw_y, pw_w, pw_h = 0, 800, 360, 160
pwr = box(pw_x, pw_y, pw_w, pw_h, "Alimentação",
          ["9 V DC center-negative", "-> LDO 5 V analógico", "-> LDO 3,3 V digital",
           "terra analógico/digital separados (star ground)"], C_PWR, tsize=13, lsize=10)

# ===================================================================== arrows
G = 3  # arrow-to-box gap

# Guitarra -> Front-End
arrow([(jack_in["x"] + 230 + G, MAIN_Y), (fe_x - G, MAIN_Y)], C_ANALOG, jack_in, fe)
label("sinal analógico,\n~0,1 a 1 V", 238, 422, C_ANALOG, maxw=132)

# Front-End -> PCM1808
arrow([(fe_x + fe_w + G, MAIN_Y), (pcm_x - G, MAIN_Y)], C_ANALOG, fe, pcm)
label("VINL", 790, 438, C_ANALOG, maxw=90)

# Route B: RP2350 is the I2S master. DOUT PCM1808 -> RP2350; MCLK / BCLK / LRCK RP2350 -> PCM1808
LBL_W = rp_x - (pcm_x + pcm_w) - 28
arrow([(pcm_x + pcm_w + G, 380), (rp_x - G, 380)], C_I2S, pcm, rp)
label("DOUT -> GP20 (I2S DIN)", pcm_x + pcm_w + 14, 364, C_I2S, maxw=LBL_W)
for yy, lab in ((420, "GP19 -> SCK (MCLK 12,288 MHz)"), (460, "GP21 -> BCK (3,072 MHz)"),
                (500, "GP22 -> LRCK (48 kHz)")):
    arrow([(rp_x - G, yy), (pcm_x + pcm_w + G, yy)], C_I2S, rp, pcm)
    label(lab, pcm_x + pcm_w + 14, yy - 16, C_I2S, maxw=LBL_W)

# RP2350 -> PCM5102A data
arrow([(rp_x + rp_w + G, 460), (dac_x - G, 460)], C_I2S, rp, dac)
label("GP23 -> DIN (I2S DOUT)", rp_x + rp_w + 10, 442, C_I2S,
      maxw=dac_x - (rp_x + rp_w) - 20)

# Shared clocks RP2350 -> PCM5102A (same nets as GP21 / GP22), routed under the RP2350.
# The LRCK route runs lower, so its vertical sits left of the BCK one (no crossing).
arrow([(1560, rp_y + rp_h + G), (1560, 740), (2150, 740), (2150, 500), (dac_x - G, 500)],
      C_I2S, rp, dac, dashed=True)
arrow([(1500, rp_y + rp_h + G), (1500, 790), (2170, 790), (2170, 530), (dac_x - G, 530)],
      C_I2S, rp, dac, dashed=True)
label("GP21 -> BCK do DAC (mesma linha do PCM1808)", 1580, 722, C_I2S, maxw=540)
label("GP22 -> LRCK do DAC (mesma linha do PCM1808)", 1520, 772, C_I2S, maxw=640)

# PCM5102A -> Jack OUT
arrow([(dac_x + dac_w + G, 480), (2650 - G, 480)], C_ANALOG, dac, jack_out)
label("VOUTL/VOUTR ->\nbuffer -> jack", 2460, 446, C_ANALOG, maxw=186)

# Pots -> Mux
arrow([(880 + G, 130), (1000 - G, 130)], C_ANALOG, pots, mux)
label("6 wipers", 892, 110, C_ANALOG, maxw=100)

# Mux -> RP2350 (ADC0)   /   RP2350 -> Mux (select)
arrow([(1260 + G, 175), (1450, 175), (1450, rp_y - G)], C_ANALOG, mux, rp)
label("saída comum -> GP26 (ADC0)", 1268, 157, C_ANALOG, maxw=178)
arrow([(1510, rp_y - G), (1510, 100), (1260 + G, 100)], C_CTRL, rp, mux)
label("GP6 / GP7 / GP8 (S0/S1/S2)", 1272, 82, C_CTRL, maxw=230)

# RP2350 <-> OLED
arrow([(1675, rp_y - G), (1675, 160 + G)], C_CTRL, rp, oled, both=True)
label("I2C0 — SDA GP4 / SCL GP5", 1686, 240, C_CTRL, maxw=250)

# Footswitch -> RP2350
arrow([(1945, 130 + G), (1945, rp_y - G)], C_CTRL, fs, rp)
label("GP14 (INPUT_PULLUP)", 1956, 205, C_CTRL, maxw=170)

# RP2350 -> LED
arrow([(1895, rp_y + rp_h + G), (1895, 650 - G)], C_CTRL, rp, led)
label("GP15", 1906, 590, C_CTRL, maxw=60)

# ---- Power: 5 V analogico (top trunk: pots + mux)
arrow([(0, 850), (-60, 850), (-60, 20), (1130, 20)], C_PWR, start=pwr, head=False)
for dx, tgt in ((740, pots), (1130, mux)):
    dot(dx, 20, C_PWR)
    arrow([(dx, 22), (dx, tgt["y"] - G)], C_PWR, end=tgt)
label("5 V analógico", 0, 0, C_PWR, size=11)

# ---- Power: 5 V analogico (bottom trunk: front-end, PCM1808, PCM5102A)
arrow([(pw_x + pw_w, 850), (2320, 850)], C_PWR, start=pwr, head=False)
for dx, tgt, ty in ((545, fe, fe_y + fe_h), (920, pcm, pcm_y + pcm_h), (2320, dac, dac_y + dac_h)):
    dot(dx, 850, C_PWR)
    arrow([(dx, 848), (dx, ty + G)], C_PWR, end=tgt)
label("5 V analógico", 400, 828, C_PWR, size=11)

# ---- Power: 3,3 V digital -> RP2350 (outer trunk above everything)
arrow([(0, 900), (-110, 900), (-110, -30), (2140, -30), (2140, 360), (rp_x + rp_w + G, 360)],
      C_V33, start=pwr, end=rp)
label("3,3 V digital", 0, -52, C_V33, size=11)

# ===================================================================== state cards
CARD_Y = 1020


def card(x, y, w, title, lines, lsize=11):
    lh = lsize * 1.25
    h = 44 + len(lines) * (lh + 5) + 10
    rect(x, y, w, h, C_CARD, PANEL, 2)
    text(title, x + 16, y + 12, 13, C_CARD, maxw=w - 32)
    ly = y + 42
    for ln in lines:
        text(ln, x + 16, ly, lsize, SUB, maxw=w - 32)
        ly += lh + 5
    return h


card(0, CARD_Y, 500, "Cadeia de DSP (Core 0, 48 kHz)", [
    "1. DC blocker",
    "2. Triode waveshaper 2x oversampled",
    "    (Plexi 1959 / JCM800 2203)",
    "3. Tone stack FMV 3ª ordem (Yeh-Smith, TDF-II)",
    "4. Presence biquad 3,5 kHz",
    "5. FIR 1024 taps (CMSIS-DSP arm_fir_f32,",
    "    Marshall 1960B)",
])
card(540, CARD_Y, 560, "Clock I2S — rota escolhida: RP2350 master", [
    "clk_sys = 245,76 MHz (divisões inteiras exatas, MCLK = clk_sys/20)",
    "MCLK 12,288 MHz -> GP19 -> SCK do PCM1808",
    "BCLK 3,072 MHz -> GP21 e LRCK 48 kHz -> GP22",
    "PCM1808 em modo escravo (MD0 = MD1 = 0, padrão de fábrica do chip)",
    "DAC PCM5102A escravo nos mesmos BCLK/LRCK",
    "96 kHz também sai exato neste clk_sys (MCLK = clk_sys/10)",
    "Rota A (oscilador de 12,288 MHz dedicado) descartada:",
    "    custa ~R$20 e exige o PIO em modo escravo",
])
card(1140, CARD_Y, 480, "Fase 0 — build verificado (30-10-2026)", [
    "pio run -e rpipico2 concluído em 147,80 s",
    "RAM 1,7%  ·  flash 0,9%",
    "firmware.uf2 gerado",
    "-mfloat-abi=hard removido após falha real",
])

# ---- legend (right of the 5 V drop to the DAC)
lx, ly0 = 2430, 650
rect(lx - 14, ly0 - 12, 270, 156, "#333333", PANEL, 1)
for i, (c, t, dashed) in enumerate((
        (C_ANALOG, "sinal analógico", False), (C_I2S, "I2S", False),
        (C_I2S, "I2S compartilhado (BCK/LRCK)", True),
        (C_CTRL, "controle / GPIO", False),
        (C_PWR, "5 V analógico", False), (C_V33, "3,3 V digital", False))):
    yy = ly0 + 6 + i * 21
    arrow([(lx, yy + 6), (lx + 36, yy + 6)], c, dashed=dashed)
    text(t, lx + 48, yy, 10, SUB, maxw=200)

# ===================================================================== write + validate
doc = {"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
       "elements": elements,
       "appState": {"gridSize": None, "viewBackgroundColor": BG},
       "files": {}}

# Merge with the live file (argv[2]): the live drawing was re-saved by Obsidian, so
#  - keep its appState / files verbatim,
#  - reuse its measured width/height for unchanged text (same text at the same x,y),
#  - carry over elements the generator never made (the pasted image), untouched, at the end.
if len(sys.argv) > 2:
    with open(sys.argv[2], encoding="utf-8") as f:
        live = json.load(f)
    doc["appState"] = live["appState"]
    doc["files"] = live["files"]
    measured = {(e["type"], e.get("text"), round(e["x"]), round(e["y"])): e
                for e in live["elements"] if e["type"] == "text"}
    gen_sigs = set()
    for e in elements:
        k = (e["type"], e.get("text"), round(e["x"]), round(e["y"]))
        gen_sigs.add(k)
        if e["type"] == "text" and k in measured:
            e["width"], e["height"] = measured[k]["width"], measured[k]["height"]
    live_ids_from_gen = {"pb%03d" % i for i in range(1, 400)}
    extra = [e for e in live["elements"] if e["id"] not in live_ids_from_gen]
    print("carried over untouched:", [(e["id"], e["type"], e["index"]) for e in extra])
    elements.extend(extra)
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(doc, f, ensure_ascii=False)
with open(OUT, encoding="utf-8") as f:
    back = json.load(f)
ids = [e["id"] for e in back["elements"]]
assert len(ids) == len(set(ids)), "duplicate ids"
idx = [e["index"] for e in back["elements"]]
assert len(idx) == len(set(idx)) and idx == sorted(idx), "index not unique/sorted"
hist = {}
for e in back["elements"]:
    hist[e["type"]] = hist.get(e["type"], 0) + 1
print("OK:", OUT, os.path.getsize(OUT), "bytes;", len(ids), "elements;", hist)
print("arrows (type=arrow):", hist.get("arrow", 0))
