#ifndef I2S_H
#define I2S_H

#include <stdint.h>

constexpr uint32_t I2S_SAMPLE_RATE   = 48000;
constexpr uint32_t I2S_BIT_DEPTH     = 24;
constexpr uint32_t I2S_SLOT_WIDTH    = 32;
constexpr uint32_t I2S_BLOCK_SIZE    = 32;
constexpr uint32_t I2S_CHANNELS      = 2;
constexpr uint32_t I2S_BLOCK_SAMPLES = I2S_BLOCK_SIZE * I2S_CHANNELS;

// Audio callback: called from DMA ISR on core 0 once per block.
// rx points to I2S_BLOCK_SAMPLES int32_t values (L0,R0,L1,R1,...) just received.
// tx points to I2S_BLOCK_SAMPLES int32_t values to fill for output.
// Data is 24-bit left-aligned in 32-bit words (bits [31:8]).
typedef void (*i2s_callback_t)(const int32_t *rx, int32_t *tx);

void i2s_init(i2s_callback_t cb);
void i2s_start();
void i2s_stop();

struct i2s_budget_t {
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t avg_cycles;
    uint32_t block_count;
};

i2s_budget_t i2s_get_budget();
void i2s_reset_budget();

#endif
