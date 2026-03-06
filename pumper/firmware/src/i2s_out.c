#include "i2s_out.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "audio_i2s.pio.h"

#define I2S_BCLK_PIN 19
#define I2S_LRCK_PIN 20
#define I2S_DIN_PIN  21

#define I2S_RING_FRAMES 2048
#define I2S_DMA_CHUNK_FRAMES 96

static PIO s_pio = pio0;
static uint s_sm = 0;
static int s_dma_chan = -1;

static volatile uint32_t s_ring[I2S_RING_FRAMES];
static volatile uint32_t s_head = 0;
static volatile uint32_t s_tail = 0;
static volatile uint32_t s_rate_hz = 48000;

static uint32_t s_dma_chunk[I2S_DMA_CHUNK_FRAMES];

static inline uint32_t ring_count(void) {
  return (s_head - s_tail) & (I2S_RING_FRAMES - 1);
}

static inline uint32_t ring_free(void) {
  return (I2S_RING_FRAMES - 1u) - ring_count();
}

static void i2s_refill_dma_chunk(void) {
  for (uint32_t i = 0; i < I2S_DMA_CHUNK_FRAMES; i++) {
    if (s_tail != s_head) {
      s_dma_chunk[i] = s_ring[s_tail];
      s_tail = (s_tail + 1u) & (I2S_RING_FRAMES - 1u);
    } else {
      s_dma_chunk[i] = 0;
    }
  }
}

static void i2s_start_dma_transfer(void) {
  i2s_refill_dma_chunk();
  dma_channel_set_read_addr(s_dma_chan, s_dma_chunk, false);
  dma_channel_set_trans_count(s_dma_chan, I2S_DMA_CHUNK_FRAMES, true);
}

void __isr i2s_dma_irq_handler(void) {
  if (!dma_channel_get_irq0_status(s_dma_chan)) {
    return;
  }
  dma_channel_acknowledge_irq0(s_dma_chan);
  i2s_start_dma_transfer();
}

void i2s_out_set_sample_rate(uint32_t sample_rate_hz) {
  if (sample_rate_hz == 0) {
    return;
  }
  s_rate_hz = sample_rate_hz;
  uint32_t clk_hz = clock_get_hz(clk_sys);
  uint32_t divider = clk_hz * 4u / sample_rate_hz;
  pio_sm_set_clkdiv_int_frac(s_pio, s_sm, divider >> 8u, divider & 0xffu);
}

void i2s_out_init(uint32_t sample_rate_hz) {
  gpio_set_function(I2S_DIN_PIN, GPIO_FUNC_PIO0);
  gpio_set_function(I2S_LRCK_PIN, GPIO_FUNC_PIO0);
  gpio_set_function(I2S_BCLK_PIN, GPIO_FUNC_PIO0);

  uint offset = pio_add_program(s_pio, &audio_i2s_program);
  audio_i2s_program_init(s_pio, s_sm, offset, I2S_DIN_PIN, I2S_LRCK_PIN);
  i2s_out_set_sample_rate(sample_rate_hz);

  s_dma_chan = dma_claim_unused_channel(true);
  dma_channel_config cfg = dma_channel_get_default_config(s_dma_chan);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&cfg, true);
  channel_config_set_write_increment(&cfg, false);
  channel_config_set_dreq(&cfg, pio_get_dreq(s_pio, s_sm, true));

  dma_channel_configure(
      s_dma_chan,
      &cfg,
      &s_pio->txf[s_sm],
      s_dma_chunk,
      I2S_DMA_CHUNK_FRAMES,
      false);

  irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_irq_handler);
  dma_channel_set_irq0_enabled(s_dma_chan, true);
  irq_set_enabled(DMA_IRQ_0, true);

  pio_sm_set_enabled(s_pio, s_sm, true);
  i2s_start_dma_transfer();
}

size_t i2s_out_write_stereo16(const int16_t *interleaved, size_t frame_count) {
  uint32_t save = save_and_disable_interrupts();
  size_t can_write = ring_free();
  if (can_write > frame_count) {
    can_write = frame_count;
  }
  for (size_t i = 0; i < can_write; i++) {
    uint16_t left = (uint16_t) interleaved[i * 2u];
    uint16_t right = (uint16_t) interleaved[i * 2u + 1u];
    s_ring[s_head] = ((uint32_t) left << 16u) | right;
    s_head = (s_head + 1u) & (I2S_RING_FRAMES - 1u);
  }
  restore_interrupts(save);
  return can_write;
}

size_t i2s_out_free_frames(void) {
  uint32_t save = save_and_disable_interrupts();
  size_t free_frames = ring_free();
  restore_interrupts(save);
  return free_frames;
}
