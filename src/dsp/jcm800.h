#ifndef JCM800_H
#define JCM800_H

// Zuko (RP2350) — JCM800-style analytical preamp model.
//
// Topology (per channel):
//   input → Stage 1: drive → asymmetric tanh clip
//         → coupling highpass (~720 Hz, 1st-order bilinear)
//         → Stage 2: drive → asymmetric tanh clip
//         → presence high-shelf (~4.5 kHz, RBJ)
//         → master scalar
//
// Same portability constraints as dsp.h: only <cmath>/<cstddef>, no Arduino,
// no pico headers, no #ifdef. Per-call state owned by caller (ISR-safe, no
// allocation). Coefficients computed once by jcm800_init().

#include <cmath>
#include <cstddef>

#include "tone_stack.h"  // BiquadCoeffs, BiquadState, biquad_sample, detail::rbj_high_shelf

namespace zuko {

// Per-channel state. Zero-initialise once: Jcm800State st{}.
struct Jcm800State {
    float hp_x_prev = 0.f;  // x[n-1] for inter-stage highpass
    float hp_y_prev = 0.f;  // y[n-1] for inter-stage highpass
    BiquadState presence{};
};

// Coefficients computed once by jcm800_init(). Immutable during processing.
struct Jcm800Coeffs {
    float drive1   = 0.f;  // stage 1 input drive
    float drive2   = 0.f;  // stage 2 input drive
    float hp_b0    = 0.f;  // inter-stage highpass numerator (b0 = -b1)
    float hp_pole  = 0.f;  // inter-stage highpass feedback pole coefficient
    float master   = 0.f;  // post-presence output scale
    BiquadCoeffs presence{};
};

// Compute JCM800 coefficients.
//   fs       — sample rate (Hz)
//   gain     [0,1] — preamp drive (0 = mild, 1 = high-gain)
//   presence [0,1] — presence shelf amount (0 = flat, 1 = +12 dB @ 4.5 kHz)
//   master   [0,1] — output level (1.0 ≈ generic preamp peak level)
inline Jcm800Coeffs jcm800_init(float fs,
                                 float gain     = 0.7f,
                                 float presence = 0.5f,
                                 float master   = 0.8f) {
    const float kPi = 3.14159265358979323846f;
    // drive 2..8 per stage
    const float d = 2.0f + gain * 6.0f;

    // Inter-stage coupling highpass, bilinear transform, fc ≈ 720 Hz.
    // H(z) = b0 * (1 - z^-1) / (1 - hp_pole * z^-1),  b0 = 1/(1+K)
    // y[n] = b0*x[n] - b0*x[n-1] + hp_pole*y[n-1]
    const float K     = std::tan(kPi * 720.0f / fs);
    const float b0    = 1.0f / (1.0f + K);
    const float pole  = (1.0f - K) / (1.0f + K);  // ~ 0.91 @ 48 kHz

    // Presence: RBJ high-shelf, fc = 4500 Hz, 0..+12 dB
    const float pres_db = presence * 12.0f;
    BiquadCoeffs pres   = detail::rbj_high_shelf(fs, 4500.0f, pres_db);

    return { d, d, b0, pole, master * 0.85f, pres };
}

// 12AX7-style asymmetric soft-clip.
// Positive half clips harder (plate toward cutoff); negative softer.
// No per-drive gain compensation: higher drive → more output + more saturation.
inline float jcm_clip(float x, float drive) {
    if (x >= 0.0f)
        return std::tanh(drive * x * 1.1f);
    else
        return std::tanh(drive * x * 0.9f);
}

// Process one sample through the full JCM800 preamp chain.
inline float jcm800_sample(float x, const Jcm800Coeffs &c, Jcm800State &st) {
    // Stage 1
    float s1 = jcm_clip(x, c.drive1);

    // Inter-stage coupling highpass (~720 Hz)
    float hp = c.hp_b0 * s1 - c.hp_b0 * st.hp_x_prev + c.hp_pole * st.hp_y_prev;
    st.hp_x_prev = s1;
    st.hp_y_prev = hp;

    // Stage 2
    float s2 = jcm_clip(hp, c.drive2);

    // Presence shelf
    float out = biquad_sample(s2, c.presence, st.presence);

    return out * c.master;
}

// Apply jcm800_sample in-place over n samples.
inline void jcm800_block(float *buf, size_t n, const Jcm800Coeffs &c, Jcm800State &st) {
    for (size_t i = 0; i < n; ++i)
        buf[i] = jcm800_sample(buf[i], c, st);
}

}  // namespace zuko

#endif  // JCM800_H
