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

// Per-block processing callback, float domain, in place.
// L and R hold n interleaved-free mono samples each, in [-1.0, +1.0].
// Phase 1: identity (pass-through). Phase 2 replaces this body only; the
// signature is what the harness and the firmware both link against.
inline void process_block(float *L, float *R, size_t n) {
    (void)L;
    (void)R;
    (void)n;
}

}  // namespace zuko

#endif  // DSP_H
