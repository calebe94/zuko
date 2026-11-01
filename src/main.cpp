// Zuko (RP2350) — Phase 0 skeleton.
//
// Proves three things and nothing else:
//   1. the toolchain builds and the board boots,
//   2. the requested system clock and core voltage actually came up,
//   3. both cores run (core 0: LED + sanity maths, core 1: heartbeat).
//
// No I2S, no DMA, no OLED yet.
//
// Phase 1 note on __not_in_flash_func: the audio hot path (per-sample DSP and
// the FIR) will be placed in RAM with the macro in this form:
//
//     float __not_in_flash_func(processSample)(float x) { ... }
//
// i.e. return type first, then __not_in_flash_func(name), then the argument
// list. Never __not_in_flash_func(float processSample)(float x).

#include <Arduino.h>
#include <math.h>
#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>

namespace {

constexpr uint8_t kPinLed = 15;                    // external LED, active high
constexpr uint32_t kTargetCpuKhz = F_CPU / 1000;   // F_CPU comes from board_build.f_cpu
constexpr uint32_t kBlinkHalfPeriodMs = 500;       // 1 Hz blink
constexpr uint32_t kHeartbeatMs = 500;
constexpr uint32_t kSerialWaitMs = 2000;           // give a USB host time to attach

constexpr int kFirTaps = 32;
constexpr int kFirIters = 1000;

// Set by core 0 once the clock is final and the banner is printed; core 1 waits
// for it so its heartbeat does not interleave with the banner.
volatile bool g_clockReady = false;

// Core voltage code -> millivolts. Only the linear 0.55 V .. 1.30 V range is
// decoded; higher codes are printed raw.
int vregMillivolts(vreg_voltage v) {
    const int code = static_cast<int>(v);
    return (code <= static_cast<int>(VREG_VOLTAGE_1_30)) ? 550 + 50 * code : -1;
}

// Tube-ish waveshaper: biased tanh with the DC offset of the bias removed.
float waveshape(float x, float drive, float bias) {
    return tanhf(drive * (x + bias)) - tanhf(drive * bias);
}

// Direct-form FIR, y[n] = sum_k h[k] * x[n-k]. Caller guarantees n >= taps-1.
float firAt(const float *x, const float *h, int n, int taps) {
    float acc = 0.0f;
    for (int k = 0; k < taps; ++k) {
        acc += h[k] * x[n - k];
    }
    return acc;
}

void printDspSanity() {
    Serial.println("-- tanhf waveshaper (drive=4, bias=0.1) --");
    const float pts[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    for (float p : pts) {
        Serial.printf("  f(% .2f) = % .6f\n", p, waveshape(p, 4.0f, 0.1f));
    }

    // 32-tap moving average over a ramp x[n] = n. At n = 40 the window covers
    // x[9..40], so the exact answer is (9 + 40) / 2 = 24.5.
    float x[64];
    float h[kFirTaps];
    for (int i = 0; i < 64; ++i) x[i] = static_cast<float>(i);
    for (int k = 0; k < kFirTaps; ++k) h[k] = 1.0f / kFirTaps;

    Serial.printf("-- %d-tap FIR on a ramp: y[40] = %.4f (expect 24.5000) --\n",
                  kFirTaps, firAt(x, h, 40, kFirTaps));

    // Timing: hardware FPU is a few cycles per multiply-accumulate, a software
    // float library is tens of cycles. The varying index keeps the compiler
    // from hoisting the work out of the loop.
    float sink = 0.0f;
    const uint32_t t0 = rp2040.getCycleCount();
    for (int i = 0; i < kFirIters; ++i) {
        sink += firAt(x, h, kFirTaps - 1 + (i & 31), kFirTaps);
    }
    const uint32_t cycles = rp2040.getCycleCount() - t0;
    const float cyclesPerMac = static_cast<float>(cycles) / (kFirIters * kFirTaps);
    Serial.printf("   %d FIRs: %lu cycles = %.2f cycles/MAC (checksum %.3f)\n",
                  kFirIters, static_cast<unsigned long>(cycles), cyclesPerMac, sink);
}

}  // namespace

void setup() {
    pinMode(kPinLed, OUTPUT);
    digitalWrite(kPinLed, LOW);

    Serial.begin(115200);

    // The core already applied F_CPU before setup(); redo it explicitly so the
    // voltage is guaranteed to be 1.20 V *before* the clock is (re)set.
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    delay(2);  // let the regulator settle
    const bool clockOk = set_sys_clock_khz(kTargetCpuKhz, false);

    const uint32_t waitStart = millis();
    while (!Serial && (millis() - waitStart) < kSerialWaitMs) {
        delay(10);
    }

    const vreg_voltage vreg = vreg_get_voltage();
    Serial.println();
    Serial.println("=== Zuko (RP2350) — Phase 0 ===");
    Serial.printf("F_CPU (configured)       : %lu Hz\n", static_cast<unsigned long>(F_CPU));
    Serial.printf("set_sys_clock_khz(%lu)  : %s\n", static_cast<unsigned long>(kTargetCpuKhz),
                  clockOk ? "ok" : "FAILED (PLL cannot hit target)");
    Serial.printf("rp2040.f_cpu()           : %d Hz\n", rp2040.f_cpu());
    Serial.printf("clk_sys (frequency counter): %lu kHz\n",
                  static_cast<unsigned long>(frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS)));
    Serial.printf("core voltage (vreg code) : 0x%02x = %d mV\n", static_cast<int>(vreg),
                  vregMillivolts(vreg));
    Serial.printf("running on core          : %u\n", get_core_num());

    printDspSanity();

    g_clockReady = true;
}

void loop() {
    static uint32_t lastToggle = 0;
    static bool ledOn = false;

    const uint32_t now = millis();
    if (now - lastToggle >= kBlinkHalfPeriodMs) {
        lastToggle = now;
        ledOn = !ledOn;
        digitalWrite(kPinLed, ledOn ? HIGH : LOW);
    }
}

void setup1() {
    // Core 1 will own UI, control and coefficient calculation (Phase 1+).
}

void loop1() {
    static uint32_t lastBeat = 0;
    static uint32_t beats = 0;

    if (!g_clockReady) return;

    const uint32_t now = millis();
    if (now - lastBeat >= kHeartbeatMs) {
        lastBeat = now;
        Serial.printf("[core %u] heartbeat #%lu t=%lu ms\n", get_core_num(),
                      static_cast<unsigned long>(++beats), static_cast<unsigned long>(now));
    }
}
