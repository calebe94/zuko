#ifndef TONE_STACK_H
#define TONE_STACK_H

// Zuko (RP2350) — 3-band RBJ-cookbook biquad tone stack.
//
// Three stages in series:
//   Bass   — low-shelf  at 100 Hz  (S=1, ±12 dB)
//   Middle — peaking EQ at 800 Hz  (Q=0.707, ±12 dB)
//   Treble — high-shelf at 3 kHz   (S=1, ±12 dB)
//
// Coefficients are computed once by tone_stack_init() and stay fixed across
// blocks.  Caller owns ToneStackState per audio channel (no globals, no
// allocation) — safe inside a DMA ISR.
//
// Same portability constraints as dsp.h: only <cmath>/<cstddef>, no Arduino,
// no pico headers, no #ifdef.

#include <cmath>
#include <cstddef>

namespace zuko {

static constexpr float kTsPi = 3.14159265358979323846f;

// RBJ biquad coefficients, a0-normalised (a0 = 1).
// Sign convention: y[n] + a1*y[n-1] + a2*y[n-2] = b0*x[n] + ...
struct BiquadCoeffs {
    float b0, b1, b2;  // numerator
    float a1, a2;      // denominator (a0 folded in)
};

// Per-channel biquad state for direct-form II transposed.
// Zero-initialise before first use: BiquadState st{}.
struct BiquadState {
    float s1 = 0.f, s2 = 0.f;
};

struct ToneStackCoeffs {
    BiquadCoeffs bass;
    BiquadCoeffs mid;
    BiquadCoeffs treble;
};

// One per audio channel; zero-initialise with {}.
struct ToneStackState {
    BiquadState bass, mid, treble;
};

namespace detail {

// RBJ low-shelf filter, S = 1 (maximum monotone slope).
// f0 is the midpoint of the shelf transition; gain at DC approaches dBgain.
inline BiquadCoeffs rbj_low_shelf(float fs, float f0, float db_gain) {
    const float A   = std::pow(10.f, db_gain / 40.f);
    const float w0  = 2.f * kTsPi * f0 / fs;
    const float cw  = std::cos(w0);
    const float sw  = std::sin(w0);
    // alpha = sin(w0)/2 * sqrt(2) when S = 1
    const float al  = sw * 0.5f * std::sqrt(2.f);
    const float sqA = std::sqrt(A);
    const float b0 =  A * ((A + 1.f) - (A - 1.f) * cw + 2.f * sqA * al);
    const float b1 =  2.f * A * ((A - 1.f) - (A + 1.f) * cw);
    const float b2 =  A * ((A + 1.f) - (A - 1.f) * cw - 2.f * sqA * al);
    const float a0 =      (A + 1.f) + (A - 1.f) * cw + 2.f * sqA * al;
    const float a1 = -2.f * ((A - 1.f) + (A + 1.f) * cw);
    const float a2 =      (A + 1.f) + (A - 1.f) * cw - 2.f * sqA * al;
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// RBJ high-shelf filter, S = 1.
// f0 is the midpoint; gain at Nyquist approaches dBgain.
inline BiquadCoeffs rbj_high_shelf(float fs, float f0, float db_gain) {
    const float A   = std::pow(10.f, db_gain / 40.f);
    const float w0  = 2.f * kTsPi * f0 / fs;
    const float cw  = std::cos(w0);
    const float sw  = std::sin(w0);
    const float al  = sw * 0.5f * std::sqrt(2.f);
    const float sqA = std::sqrt(A);
    const float b0 =  A * ((A + 1.f) + (A - 1.f) * cw + 2.f * sqA * al);
    const float b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cw);
    const float b2 =  A * ((A + 1.f) + (A - 1.f) * cw - 2.f * sqA * al);
    const float a0 =      (A + 1.f) - (A - 1.f) * cw + 2.f * sqA * al;
    const float a1 =  2.f * ((A - 1.f) - (A + 1.f) * cw);
    const float a2 =      (A + 1.f) - (A - 1.f) * cw - 2.f * sqA * al;
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

// RBJ peaking EQ.  The gain at the centre frequency w0 is exactly A^2 = 10^(dBgain/20).
// Q = 0.707 gives approximately one-octave bandwidth.
inline BiquadCoeffs rbj_peaking(float fs, float f0, float db_gain, float Q = 0.707f) {
    const float A   = std::pow(10.f, db_gain / 40.f);
    const float w0  = 2.f * kTsPi * f0 / fs;
    const float cw  = std::cos(w0);
    const float al  = std::sin(w0) / (2.f * Q);
    const float b0 =  1.f + al * A;
    const float b1 = -2.f * cw;
    const float b2 =  1.f - al * A;
    const float a0 =  1.f + al / A;
    const float a1 = -2.f * cw;
    const float a2 =  1.f - al / A;
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

}  // namespace detail

// Compute ToneStackCoeffs for the given sample rate and band gains (dB).
// Gains outside [-12, +12] dB are clamped.
inline ToneStackCoeffs tone_stack_init(float fs,
                                        float bass_db   = 0.f,
                                        float mid_db    = 0.f,
                                        float treble_db = 0.f) {
    auto clamp12 = [](float v) { return v < -12.f ? -12.f : v > 12.f ? 12.f : v; };
    return {
        detail::rbj_low_shelf (fs, 100.f,  clamp12(bass_db)),
        detail::rbj_peaking   (fs, 800.f,  clamp12(mid_db), 0.707f),
        detail::rbj_high_shelf(fs, 3000.f, clamp12(treble_db)),
    };
}

// Process one sample through one biquad stage, direct-form II transposed.
// y[n] = b0*x[n] + s1
// s1'  = b1*x[n] - a1*y[n] + s2
// s2'  = b2*x[n] - a2*y[n]
inline float biquad_sample(float x, const BiquadCoeffs &c, BiquadState &s) {
    const float y = c.b0 * x + s.s1;
    s.s1 = c.b1 * x - c.a1 * y + s.s2;
    s.s2 = c.b2 * x - c.a2 * y;
    return y;
}

// Apply all three biquad stages in series (bass → mid → treble), in-place.
inline void tone_stack_block(float *buf, size_t n,
                              const ToneStackCoeffs &c, ToneStackState &st) {
    for (size_t i = 0; i < n; ++i) {
        float s = buf[i];
        s = biquad_sample(s, c.bass,   st.bass);
        s = biquad_sample(s, c.mid,    st.mid);
        s = biquad_sample(s, c.treble, st.treble);
        buf[i] = s;
    }
}

}  // namespace zuko

#endif  // TONE_STACK_H
