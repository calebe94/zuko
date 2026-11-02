#!/usr/bin/env python3
"""wav2header.py — convert a 48 kHz IR WAV to a C float-array header.

Usage:
  python3 wav2header.py ir.wav -o src/dsp/cab_ir.h [--taps 512]
  python3 wav2header.py --analytic -o src/dsp/cab_ir.h [--taps 1024]

--analytic generates a windowed-sinc cabinet IR without an external file:
  - first-order lowpass at ~4500 Hz (rolled into the sinc design)
  - mild low-shelf rolloff below ~90 Hz (a second-order IIR factored out of
    the linear phase design — approximated via frequency-domain shaping of
    the prototype sinc)
  - Hamming window
  - normalised to unit peak

The script always prints energy retention when --taps N < len(IR).
"""

import argparse
import math
import struct
import sys
import wave


# ---------------------------------------------------------------------------
# Windowed-sinc helpers (stdlib only)
# ---------------------------------------------------------------------------

def hamming(n: int) -> list[float]:
    if n == 1:
        return [1.0]
    return [0.54 - 0.46 * math.cos(2.0 * math.pi * k / (n - 1)) for k in range(n)]


def sinc_lp(fc_norm: float, n_taps: int) -> list[float]:
    """Linear-phase lowpass FIR, fc_norm in [0, 0.5] (fraction of fs)."""
    half = (n_taps - 1) / 2.0
    win = hamming(n_taps)
    h = []
    for k in range(n_taps):
        t = k - half
        if abs(t) < 1e-12:
            h.append(2.0 * fc_norm * win[k])
        else:
            h.append(math.sin(2.0 * math.pi * fc_norm * t) / (math.pi * t) * win[k])
    return h


def apply_low_shelf(h: list[float], fs: float, f0: float, gain_db: float) -> list[float]:
    """Convolve h with a 2-tap FIR approximation of a first-order low-shelf.

    A true minimum-phase low-shelf would require IIR processing; here we use
    a two-sample FIR approximation that gives the right DC/HF ratio without
    breaking the linear-phase property of the windowed sinc.  The rolloff is
    gentle — appropriate for a plausible 4x12 low-end sag.
    """
    # Bilinear first-order shelf: mix a leaky integrator with identity.
    # alpha controls the shelf corner: alpha = exp(-2*pi*f0/fs)
    alpha = math.exp(-2.0 * math.pi * f0 / fs)
    gain_lin = 10.0 ** (gain_db / 20.0)
    # Two-tap: [1 + (gain_lin - 1)*alpha_dc_weight, -(gain_lin - 1)*alpha_dc_weight]
    # We approximate as: h_shelf[n] = h[n] + (gain_lin - 1) * alpha * h[n-1]
    out = list(h)
    g = (gain_lin - 1.0) * alpha
    for i in range(1, len(out)):
        out[i] = h[i] + g * h[i - 1]
    return out


def generate_analytic_ir(n_taps: int, fs: float = 48000.0) -> list[float]:
    """Analytical 4x12-ish cabinet IR.

    Shape:
      - Lowpass corner ~4500 Hz  (windowed-sinc Hamming)
      - Low-shelf sag below ~90 Hz, -3 dB at DC
    """
    fc = 4500.0 / fs          # normalised cutoff
    h = sinc_lp(fc, n_taps)
    # Low-shelf: -3 dB at DC, shelf corner at 90 Hz
    h = apply_low_shelf(h, fs, f0=90.0, gain_db=-3.0)
    return h


# ---------------------------------------------------------------------------
# WAV reader (stdlib wave module)
# ---------------------------------------------------------------------------

def read_wav_ir(path: str) -> tuple[list[float], int]:
    """Return (samples_float, sample_rate).  Only first channel used."""
    with wave.open(path, 'rb') as wf:
        rate = wf.getframerate()
        n_frames = wf.getnframes()
        sampwidth = wf.getsampwidth()
        n_channels = wf.getnchannels()
        raw = wf.readframes(n_frames)

    fmt = {1: 'b', 2: 'h', 4: 'i'}.get(sampwidth)
    if fmt is None:
        raise ValueError(f"Unsupported sample width {sampwidth}")
    scale = 1.0 / (2 ** (sampwidth * 8 - 1))
    step = n_channels
    samples = [
        struct.unpack_from(fmt, raw, i * sampwidth)[0] * scale
        for i in range(0, n_frames * n_channels, step)
    ]
    return samples, rate


# ---------------------------------------------------------------------------
# Truncate + energy report
# ---------------------------------------------------------------------------

def truncate(h: list[float], n_taps: int) -> tuple[list[float], float]:
    """Truncate to n_taps, return (truncated, energy_fraction)."""
    e_full = sum(x * x for x in h)
    if n_taps >= len(h):
        return h, 1.0
    trunc = h[:n_taps]
    e_trunc = sum(x * x for x in trunc)
    frac = e_trunc / e_full if e_full > 0 else 1.0
    return trunc, frac


# ---------------------------------------------------------------------------
# Normalise
# ---------------------------------------------------------------------------

def normalise(h: list[float]) -> list[float]:
    """Normalise to unit peak, but also cap DC gain to ≤ 1.

    For real cabinet IRs the initial transient peak >> DC sum, so we divide by
    peak (standard unit-peak normalisation).  For an analytic narrow-band LP
    the DC sum > peak (5× for a 4500 Hz LP at 48 kHz), so dividing by peak
    alone would give 14 dB passband gain — physically wrong.  We therefore
    divide by max(peak, |dc_sum|), which is identical to unit-peak for typical
    real IRs and gives unit-DC-gain for the analytic LP.
    """
    peak = max(abs(x) for x in h)
    dc_sum = abs(sum(h))
    scale = max(peak, dc_sum)
    if scale < 1e-30:
        return h
    return [x / scale for x in h]


# ---------------------------------------------------------------------------
# Header writer
# ---------------------------------------------------------------------------

HEADER_TEMPLATE = """\
// Auto-generated by wav2header.py — DO NOT EDIT.
// {source_desc}
// Taps: {n_taps}  Energy retained: {energy_pct:.2f}%
#ifndef CAB_IR_H
#define CAB_IR_H

namespace zuko {{

constexpr int kCabFirTaps = {n_taps};

// Normalised impulse response coefficients (float32, peak = 1.0).
// Index 0 = h[0] (most recent sample in the convolution).
static const float kCabFirCoeffs[kCabFirTaps] = {{
{coeff_lines}
}};

}}  // namespace zuko

#endif  // CAB_IR_H
"""

def coeffs_to_lines(h: list[float], cols: int = 4) -> str:
    lines = []
    for i in range(0, len(h), cols):
        chunk = h[i:i + cols]
        parts = [f"{v:.9e}f" for v in chunk]
        lines.append("    " + ", ".join(parts) + ",")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wav", nargs="?", help="Input 48 kHz mono/stereo WAV")
    ap.add_argument("--analytic", action="store_true",
                    help="Generate analytical 4x12-ish IR (no WAV needed)")
    ap.add_argument("--taps", type=int, default=1024,
                    help="Number of taps to emit (default 1024)")
    ap.add_argument("-o", "--output", default="-",
                    help="Output header path (default stdout)")
    args = ap.parse_args()

    if not args.analytic and args.wav is None:
        ap.error("Provide a WAV file or use --analytic")

    if args.analytic:
        raw = generate_analytic_ir(max(args.taps, 1024))
        source_desc = f"Analytical 4x12-ish IR (Hamming-sinc LP @4500 Hz + low-shelf @90 Hz, fs=48000 Hz)"
    else:
        raw, rate = read_wav_ir(args.wav)
        if rate != 48000:
            print(f"WARNING: WAV sample rate is {rate} Hz, expected 48000", file=sys.stderr)
        source_desc = f"Source: {args.wav}"

    h, energy_frac = truncate(raw, args.taps)
    h = normalise(h)

    energy_pct = energy_frac * 100.0
    print(f"Taps: {len(h)}  Energy retained: {energy_pct:.2f}%", file=sys.stderr)

    body = HEADER_TEMPLATE.format(
        source_desc=source_desc,
        n_taps=len(h),
        energy_pct=energy_pct,
        coeff_lines=coeffs_to_lines(h),
    )

    if args.output == "-":
        sys.stdout.write(body)
    else:
        with open(args.output, "w") as f:
            f.write(body)
        print(f"Wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
