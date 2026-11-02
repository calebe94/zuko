// Zuko (RP2350) — Phase 1: I2S audio pass-through.
//
// Core 0 runs the audio path only:
//   1. the Phase 0 clock/voltage bring-up and FPU sanity maths (unchanged),
//   2. the I2S driver (PIO master clocks + DMA ping-pong) over a 32-sample block,
//   3. the pass-through callback: copy the received block to the transmit block,
//   4. the block-processing budget report (min/avg/max cycles per block).
// Core 1 keeps the Phase 0 heartbeat and is otherwise idle (UI/control is Phase 4).
//
// __not_in_flash_func syntax, now used for real: the audio hot path is placed in
// RAM with the macro in this form:
//
//     float __not_in_flash_func(processSample)(float x) { ... }
//
// i.e. return type first, then __not_in_flash_func(name), then the argument
// list. Never __not_in_flash_func(float processSample)(float x).

#include <Arduino.h>
#include <cstdint>
#include <math.h>
#include <string.h>
#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/vreg.h>

#include "i2s.h"
#include "dsp/dsp.h"

namespace {

using zuko::waveshape;
using zuko::fir_at;


constexpr uint8_t kPinLed = 15;                    // external LED, active high
constexpr uint32_t kTargetCpuKhz = F_CPU / 1000;   // F_CPU comes from board_build.f_cpu
constexpr uint32_t kBlinkHalfPeriodMs = 500;       // 1 Hz blink
constexpr uint32_t kHeartbeatMs = 500;
constexpr uint32_t kSerialWaitMs = 2000;           // give a USB host time to attach
constexpr uint32_t kBudgetReportBlocks = 100;      // one block is 666.7 us -> ~67 ms

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

void printDspSanity() {
    Serial.println("-- tanhf waveshaper (drive=4, bias=0.1) --");
    const float pts[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
    for (float p : pts) {
        Serial.printf("  f(% .2f) = % .6f\n", p, waveshape(p, 4.0f, 0.1f));
    }

    // 32-tap moving average over a ramp x[n] = n. At n = 40 the window covers
    // x[9..40], so the exact answer is (9 + 40) / 2 = 24.5.
    float x[64];
    float h[zuko::kFirTaps];
    for (int i = 0; i < 64; ++i) x[i] = static_cast<float>(i);
    for (int k = 0; k < zuko::kFirTaps; ++k) h[k] = 1.0f / zuko::kFirTaps;

    Serial.printf("-- %d-tap FIR on a ramp: y[40] = %.4f (expect 24.5000) --\n",
                  zuko::kFirTaps, fir_at(x, h, 40, zuko::kFirTaps));

    // Timing: hardware FPU is a few cycles per multiply-accumulate, a software
    // float library is tens of cycles. The varying index keeps the compiler
    // from hoisting the work out of the loop.
    float sink = 0.0f;
    const uint32_t t0 = rp2040.getCycleCount();
    for (int i = 0; i < kFirIters; ++i) {
        sink += fir_at(x, h, zuko::kFirTaps - 1 + (i & 31), zuko::kFirTaps);
    }
    const uint32_t cycles = rp2040.getCycleCount() - t0;
    const float cyclesPerMac = static_cast<float>(cycles) / (kFirIters * zuko::kFirTaps);
    Serial.printf("   %d FIRs: %lu cycles = %.2f cycles/MAC (checksum %.3f)\n",
                  kFirIters, static_cast<unsigned long>(cycles), cyclesPerMac, sink);
}

// The I2S consumer/producer callback for this phase: copy the 32 received
// samples (both channels, L0,R0,L1,R1,...) to the 32 transmitted samples. No
// gain, no filtering, no processing — the point of Phase 1 is that audio
// survives the round trip. Called from the DMA IRQ on core 0, from RAM.
void __not_in_flash_func(audio_passthrough)(const int32_t *rx, int32_t *tx) {
    memcpy(tx, rx, I2S_BLOCK_SAMPLES * sizeof(int32_t));
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
    Serial.println("=== Zuko (RP2350) — Phase 1 ===");
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

    Serial.println();
    Serial.println("-- I2S init --");
    Serial.printf("Sample rate  : %lu Hz\n", static_cast<unsigned long>(I2S_SAMPLE_RATE));
    Serial.printf("Bit depth    : %lu in %lu-bit slots\n",
                  static_cast<unsigned long>(I2S_BIT_DEPTH),
                  static_cast<unsigned long>(I2S_SLOT_WIDTH));
    Serial.printf("Block size   : %lu samples/ch (%lu total)\n",
                  static_cast<unsigned long>(I2S_BLOCK_SIZE),
                  static_cast<unsigned long>(I2S_BLOCK_SAMPLES));

    i2s_init(audio_passthrough);
    i2s_start();

    Serial.println("I2S started — pass-through active.");

    g_clockReady = true;
}

void loop() {
    static uint32_t lastToggle = 0;
    static uint32_t lastBudget = 0;
    static bool ledOn = false;

    const uint32_t now = millis();

    if (now - lastToggle >= kBlinkHalfPeriodMs) {
        lastToggle = now;
        ledOn = !ledOn;
        digitalWrite(kPinLed, ledOn ? HIGH : LOW);
    }

    // Budget report, printed from loop() and never from the audio ISR: a slow
    // USB host must not be able to stall the audio path.
    i2s_budget_t b = i2s_get_budget();
    if (b.block_count >= kBudgetReportBlocks && (now - lastBudget >= 1000)) {
        lastBudget = now;
        constexpr uint32_t kBudgetTotal = 163840;  // cycles of wall-clock budget per block
        float avg_pct = 100.0f * static_cast<float>(b.avg_cycles) / kBudgetTotal;
        float max_pct = 100.0f * static_cast<float>(b.max_cycles) / kBudgetTotal;
        Serial.printf("[budget] min=%lu avg=%lu max=%lu cycles  (avg %.1f%% max %.1f%% of %lu)\n",
                      static_cast<unsigned long>(b.min_cycles),
                      static_cast<unsigned long>(b.avg_cycles),
                      static_cast<unsigned long>(b.max_cycles),
                      avg_pct, max_pct,
                      static_cast<unsigned long>(kBudgetTotal));
        i2s_reset_budget();
    }

    // for (uint8_t io = 0; io < 8; io++)
    // {
    //     io_write(io&0xFF); // write to the io register
    //     read(cd4051_in);
    // }
}

void setup1() {
    // Core 1: UI, control, coefficient calculation (idle in Phase 1).
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
