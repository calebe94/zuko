#ifndef DSP_H
#define DSP_H

// Zuko (RP2350) — hardware-independent DSP core.
//
// This header must stay compilable by BOTH the RP2350 cross-compiler (Docker,
// Arduino/earlephilhower core) and the host compiler on the Mac (host/Makefile).
// That means: no Arduino.h, no pico/ headers, no C++ headers beyond <cstdint>,
// <cstddef> and <cmath>. Any platform-specific need gets an explicit discussion
// before an #ifdef is added (there are none today).

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "preamp.h"
#include "tone_stack.h"

namespace zuko {

constexpr int kFirTaps = 32;  // moving-average FIR used as the FPU sanity check

// Tube-ish waveshaper: biased tanh with the DC offset of the bias removed.
// Phase 2 material; kept because it is the FPU sanity check.
inline float waveshape(float x, float drive, float bias) {
    return std::tanh(drive * (x + bias)) - std::tanh(drive * bias);
}

// Direct-form FIR, y[n] = sum_k h[k] * x[n-k]. Caller guarantees n >= taps-1.
inline float fir_at(const float *x, const float *h, int n, int taps) {
    float acc = 0.0f;
    for (int k = 0; k < taps; ++k) {
        acc += h[k] * x[n - k];
    }
    return acc;
}

// Caller-owned DSP state: biquad filter memory for L and R channels.
// Zero-initialise once before the first process_block call: DspState state{}.
struct DspState {
    ToneStackState ts_L;
    ToneStackState ts_R;
};

// Per-block processing: preamp → 3-band tone stack, in place.
// coeffs are computed once by tone_stack_init() and stay fixed across blocks.
// state carries the biquad delay lines between blocks (caller owns, no globals).
inline void process_block(float *L, float *R, size_t n,
                           DspState &state, const ToneStackCoeffs &coeffs) {
    preamp_block(L, n, kPreampDrive, kPreampLevel);
    preamp_block(R, n, kPreampDrive, kPreampLevel);
    tone_stack_block(L, n, coeffs, state.ts_L);
    tone_stack_block(R, n, coeffs, state.ts_R);
}

}  // namespace zuko

#endif  // DSP_H
