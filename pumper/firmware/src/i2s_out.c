#include "i2s_out.h"

#include <limits.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/critical_section.h"
#include "pico/stdlib.h"

#include "audio_i2s.pio.h"

#define I2S_BCLK_PIN 19u
#define I2S_LRCK_PIN 20u
#define I2S_DIN_PIN  21u

#define I2S_RING_FRAMES 8192u
#define I2S_DMA_BUFFER_COUNT 2u
#define I2S_DMA_MAX_CHUNK_FRAMES 192u

static PIO s_pio = pio0;
static uint s_sm = 0u;
static int s_dma_chan[I2S_DMA_BUFFER_COUNT] = {-1, -1};

static uint32_t s_ring[I2S_RING_FRAMES];
static uint32_t s_head = 0u;
static uint32_t s_tail = 0u;
static volatile uint32_t s_rate_hz = 48000u;
static volatile bool s_streaming = false;
static bool s_priming = true;
static uint32_t s_chunk_phase = 0u;
static volatile uint32_t s_underrun_frames = 0u;
static volatile uint32_t s_low_water_frames = UINT32_MAX;
static critical_section_t s_ring_lock;

static uint32_t s_dma_chunk[I2S_DMA_BUFFER_COUNT][I2S_DMA_MAX_CHUNK_FRAMES];

static inline uint32_t ring_count(void) {
  return (s_head - s_tail) & (I2S_RING_FRAMES - 1u);
}

static inline uint32_t ring_free(void) {
  return (I2S_RING_FRAMES - 1u) - ring_count();
}

static uint32_t prime_target_frames(void) {
  return (s_rate_hz * 2u + 999u) / 1000u;
}

static uint32_t next_chunk_frames(void) {
  uint32_t frames = s_rate_hz / 1000u;
  s_chunk_phase += s_rate_hz % 1000u;
  if (s_chunk_phase >= 1000u) {
    s_chunk_phase -= 1000u;
    frames++;
  }
  if (frames == 0u) frames = 1u;
  if (frames > I2S_DMA_MAX_CHUNK_FRAMES) frames = I2S_DMA_MAX_CHUNK_FRAMES;
  return frames;
}

static void copy_ring_to_chunk(uint32_t *chunk, uint32_t frame_count) {
  critical_section_enter_blocking(&s_ring_lock);
  uint32_t available = ring_count();
  if (!s_streaming || s_priming) {
    if (s_streaming && available >= prime_target_frames()) s_priming = false;
  }

  if (!s_streaming || s_priming) {
    memset(chunk, 0, frame_count * sizeof(*chunk));
    critical_section_exit(&s_ring_lock);
    return;
  }

  if (available < frame_count) {
    memset(chunk, 0, frame_count * sizeof(*chunk));
    s_underrun_frames += frame_count;
    s_priming = true;
    critical_section_exit(&s_ring_lock);
    return;
  }

  uint32_t first = I2S_RING_FRAMES - s_tail;
  if (first > frame_count) first = frame_count;
  memcpy(chunk, &s_ring[s_tail], first * sizeof(*chunk));
  if (first < frame_count) {
    memcpy(chunk + first, s_ring, (frame_count - first) * sizeof(*chunk));
  }
  s_tail = (s_tail + frame_count) & (I2S_RING_FRAMES - 1u);
  uint32_t remaining = available - frame_count;
  if (remaining < s_low_water_frames) s_low_water_frames = remaining;
  critical_section_exit(&s_ring_lock);
}

static void prepare_dma_buffer(uint32_t index) {
  uint32_t frames = next_chunk_frames();
  copy_ring_to_chunk(s_dma_chunk[index], frames);
  dma_channel_set_read_addr((uint)s_dma_chan[index], s_dma_chunk[index], false);
  dma_channel_set_trans_count((uint)s_dma_chan[index], frames, false);
}

static void restart_dma(void) {
  if (s_dma_chan[0] < 0 || s_dma_chan[1] < 0) return;
  irq_set_enabled(DMA_IRQ_0, false);
  dma_channel_abort((uint)s_dma_chan[0]);
  dma_channel_abort((uint)s_dma_chan[1]);
  dma_channel_acknowledge_irq0((uint)s_dma_chan[0]);
  dma_channel_acknowledge_irq0((uint)s_dma_chan[1]);
  s_chunk_phase = 0u;
  prepare_dma_buffer(0u);
  prepare_dma_buffer(1u);
  dma_start_channel_mask(1u << (uint)s_dma_chan[0]);
  irq_set_enabled(DMA_IRQ_0, true);
}

void __isr i2s_dma_irq_handler(void) {
  for (uint32_t index = 0u; index < I2S_DMA_BUFFER_COUNT; index++) {
    uint channel = (uint)s_dma_chan[index];
    if (dma_channel_get_irq0_status(channel)) {
      dma_channel_acknowledge_irq0(channel);
      prepare_dma_buffer(index);
    }
  }
}

void i2s_out_set_sample_rate(uint32_t sample_rate_hz) {
  if (sample_rate_hz == 0u) return;
  s_rate_hz = sample_rate_hz;
  uint32_t clk_hz = clock_get_hz(clk_sys);
  uint32_t div_8_8 = (clk_hz * 4u + sample_rate_hz / 2u) / sample_rate_hz;
  pio_sm_set_clkdiv_int_frac(s_pio, s_sm, div_8_8 >> 8u, div_8_8 & 0xffu);

  critical_section_enter_blocking(&s_ring_lock);
  s_head = 0u;
  s_tail = 0u;
  s_priming = true;
  s_low_water_frames = UINT32_MAX;
  critical_section_exit(&s_ring_lock);
  restart_dma();
}

void i2s_out_init(uint32_t sample_rate_hz) {
  critical_section_init(&s_ring_lock);
  gpio_set_function(I2S_DIN_PIN, GPIO_FUNC_PIO0);
  gpio_set_function(I2S_LRCK_PIN, GPIO_FUNC_PIO0);
  gpio_set_function(I2S_BCLK_PIN, GPIO_FUNC_PIO0);

  uint offset = pio_add_program(s_pio, &audio_i2s_program);
  audio_i2s_program_init(s_pio, s_sm, offset, I2S_DIN_PIN, I2S_BCLK_PIN);
  s_rate_hz = sample_rate_hz;
  uint32_t clk_hz = clock_get_hz(clk_sys);
  uint32_t div_8_8 = (clk_hz * 4u + sample_rate_hz / 2u) / sample_rate_hz;
  pio_sm_set_clkdiv_int_frac(s_pio, s_sm, div_8_8 >> 8u, div_8_8 & 0xffu);

  for (uint32_t index = 0u; index < I2S_DMA_BUFFER_COUNT; index++) {
    s_dma_chan[index] = dma_claim_unused_channel(true);
  }
  for (uint32_t index = 0u; index < I2S_DMA_BUFFER_COUNT; index++) {
    dma_channel_config cfg = dma_channel_get_default_config((uint)s_dma_chan[index]);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_chain_to(&cfg, (uint)s_dma_chan[index ^ 1u]);
    dma_channel_configure((uint)s_dma_chan[index], &cfg, &s_pio->txf[s_sm],
                          s_dma_chunk[index], 1u, false);
    dma_channel_set_irq0_enabled((uint)s_dma_chan[index], true);
  }

  irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_irq_handler);
  irq_set_enabled(DMA_IRQ_0, true);
  pio_sm_set_enabled(s_pio, s_sm, true);
  restart_dma();
}

size_t i2s_out_write_stereo16(const int16_t *interleaved, size_t frame_count) {
  if (interleaved == NULL || frame_count == 0u || !s_streaming) return 0u;
  critical_section_enter_blocking(&s_ring_lock);
  size_t can_write = ring_free();
  if (can_write > frame_count) can_write = frame_count;
  uint32_t first = I2S_RING_FRAMES - s_head;
  if (first > can_write) first = (uint32_t)can_write;
  memcpy(&s_ring[s_head], interleaved, first * sizeof(uint32_t));
  if (first < can_write) {
    memcpy(s_ring, interleaved + first * 2u, (can_write - first) * sizeof(uint32_t));
  }
  s_head = (s_head + (uint32_t)can_write) & (I2S_RING_FRAMES - 1u);
  critical_section_exit(&s_ring_lock);
  return can_write;
}

size_t i2s_out_free_frames(void) {
  critical_section_enter_blocking(&s_ring_lock);
  size_t free_frames = ring_free();
  critical_section_exit(&s_ring_lock);
  return free_frames;
}

uint32_t i2s_out_buffered_frames(void) {
  critical_section_enter_blocking(&s_ring_lock);
  uint32_t count = ring_count();
  critical_section_exit(&s_ring_lock);
  return count;
}

void i2s_out_set_streaming(bool streaming) {
  critical_section_enter_blocking(&s_ring_lock);
  s_streaming = streaming;
  s_head = 0u;
  s_tail = 0u;
  s_priming = true;
  s_low_water_frames = UINT32_MAX;
  critical_section_exit(&s_ring_lock);
  restart_dma();
}

uint32_t i2s_out_underrun_frames(void) {
  critical_section_enter_blocking(&s_ring_lock);
  uint32_t count = s_underrun_frames;
  critical_section_exit(&s_ring_lock);
  return count;
}

uint32_t i2s_out_low_water_frames(void) {
  critical_section_enter_blocking(&s_ring_lock);
  uint32_t count = s_low_water_frames == UINT32_MAX ? 0u : s_low_water_frames;
  critical_section_exit(&s_ring_lock);
  return count;
}
