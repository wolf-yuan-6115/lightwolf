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
#define I2S_DIN_PIN 21u
#define DMA_COUNT 2u
#define SILENCE_MAX_WORDS 384u

static PIO const pio = pio0;
static uint const sm = 0u;
static int dma_chan[DMA_COUNT] = {-1, -1};
static uint offset16, offset32;
static uint32_t rate_hz = 48000u, silence_phase;
static audio_sample_format_t active_format = AUDIO_FORMAT_PCM16;
static volatile uint32_t underrun_frames, low_water_frames = UINT32_MAX;
static audio_block_queue_t block_queue;
static i2s_audio_block_t *active[DMA_COUNT];
static uint32_t silence[SILENCE_MAX_WORDS];
static critical_section_t lock;
static i2s_block_release_fn release_block;

static void configure_normal_dma(void) {
  for (uint32_t i = 0; i < DMA_COUNT; i++) {
    dma_channel_config cfg = dma_channel_get_default_config((uint)dma_chan[i]);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, true));
    channel_config_set_chain_to(&cfg, (uint)dma_chan[i ^ 1u]);
    dma_channel_configure((uint)dma_chan[i], &cfg, &pio->txf[sm], silence,
                          dma_encode_transfer_count(1u), false);
    dma_channel_set_irq0_enabled((uint)dma_chan[i], true);
  }
}

static uint32_t next_silence_frames(void) {
  uint32_t frames = rate_hz / 1000u;
  silence_phase += rate_hz % 1000u;
  if (silence_phase >= 1000u) { silence_phase -= 1000u; frames++; }
  return frames;
}

static void prepare_dma(uint32_t index) {
  i2s_audio_block_t *block = NULL;
  uint32_t frames;
  bool starved = false;
  critical_section_enter_blocking(&lock);
  block = (i2s_audio_block_t *)audio_block_queue_take(&block_queue, &starved);
  active[index] = block;
  critical_section_exit(&lock);

  uint32_t const *source;
  uint32_t words;
  if (block) {
    source = block->data.words;
    words = block->word_count;
  } else {
    frames = next_silence_frames();
    if (starved) underrun_frames += frames;
    words = frames * (active_format == AUDIO_FORMAT_PCM24 ? 2u : 1u);
    source = silence;
  }
  dma_channel_set_read_addr((uint)dma_chan[index], source, false);
  dma_channel_set_trans_count((uint)dma_chan[index], words, false);
}

static void restart_dma(void) {
  if (dma_chan[0] < 0) return;
  irq_set_enabled(DMA_IRQ_0, false);
  for (uint32_t i = 0; i < DMA_COUNT; i++) {
    dma_channel_abort((uint)dma_chan[i]);
    dma_channel_acknowledge_irq0((uint)dma_chan[i]);
  }
  configure_normal_dma();
  silence_phase = 0u;
  prepare_dma(0u);
  prepare_dma(1u);
  dma_start_channel_mask(1u << (uint)dma_chan[0]);
  irq_set_enabled(DMA_IRQ_0, true);
}

void __isr i2s_dma_irq_handler(void) {
  for (uint32_t i = 0; i < DMA_COUNT; i++) {
    uint channel = (uint)dma_chan[i];
    if (!dma_channel_get_irq0_status(channel)) continue;
    dma_channel_acknowledge_irq0(channel);
    i2s_audio_block_t *done = active[i];
    if (done) {
      critical_section_enter_blocking(&lock);
      audio_block_queue_complete(&block_queue, done->frames);
      if (block_queue.buffered_frames < low_water_frames)
        low_water_frames = block_queue.buffered_frames;
      critical_section_exit(&lock);
      active[i] = NULL;
      release_block(done);
    }
    prepare_dma(i);
  }
}

static void configure_pio(void) {
  pio_sm_set_enabled(pio, sm, false);
  pio_sm_clear_fifos(pio, sm);
  pio_sm_restart(pio, sm);
  uint offset, entry;
  pio_sm_config config;
  uint32_t divider;
  if (active_format == AUDIO_FORMAT_PCM24) {
    offset = offset32; entry = audio_i2s_32_offset_entry32;
    config = audio_i2s_32_program_get_default_config(offset);
    divider = (clock_get_hz(clk_sys) * 2u + rate_hz / 2u) / rate_hz;
  } else {
    offset = offset16; entry = audio_i2s_16_offset_entry16;
    config = audio_i2s_16_program_get_default_config(offset);
    divider = (clock_get_hz(clk_sys) * 4u + rate_hz / 2u) / rate_hz;
  }
  audio_i2s_program_init(pio, sm, offset, entry, I2S_DIN_PIN, I2S_BCLK_PIN, config);
  pio_sm_set_clkdiv_int_frac(pio, sm, divider >> 8u, divider & 0xffu);
  pio_sm_set_enabled(pio, sm, true);
}

void i2s_out_init(uint32_t sample_rate_hz, audio_sample_format_t format,
                  i2s_block_release_fn release) {
  critical_section_init(&lock);
  release_block = release;
  gpio_set_function(I2S_DIN_PIN, GPIO_FUNC_PIO0);
  gpio_set_function(I2S_LRCK_PIN, GPIO_FUNC_PIO0);
  gpio_set_function(I2S_BCLK_PIN, GPIO_FUNC_PIO0);
  offset16 = pio_add_program(pio, &audio_i2s_16_program);
  offset32 = pio_add_program(pio, &audio_i2s_32_program);
  rate_hz = sample_rate_hz;
  active_format = format;
  audio_block_queue_reset(&block_queue, rate_hz, false);
  configure_pio();
  for (uint32_t i = 0; i < DMA_COUNT; i++) dma_chan[i] = dma_claim_unused_channel(true);
  configure_normal_dma();
  irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_irq_handler);
  irq_set_enabled(DMA_IRQ_0, true);
  restart_dma();
}

bool i2s_out_submit(i2s_audio_block_t *block) {
  if (!block) return false;
  critical_section_enter_blocking(&lock);
  bool accepted = block->format == active_format &&
                  audio_block_queue_submit(&block_queue, block, block->frames);
  critical_section_exit(&lock);
  return accepted;
}

void i2s_out_set_format(uint32_t sample_rate_hz, audio_sample_format_t format) {
  i2s_out_set_streaming(false);
  rate_hz = sample_rate_hz;
  active_format = format;
  critical_section_enter_blocking(&lock);
  block_queue.sample_rate_hz = rate_hz;
  critical_section_exit(&lock);
  configure_pio();
  restart_dma();
}

void i2s_out_set_streaming(bool enabled) {
  i2s_audio_block_t *reclaim[I2S_AUDIO_BLOCK_COUNT + DMA_COUNT];
  void *queued[I2S_AUDIO_BLOCK_COUNT];
  size_t count = 0;
  irq_set_enabled(DMA_IRQ_0, false);
  if (dma_chan[0] >= 0) {
    dma_channel_abort((uint)dma_chan[0]);
    dma_channel_abort((uint)dma_chan[1]);
  }
  critical_section_enter_blocking(&lock);
  size_t queued_count = audio_block_queue_drain(&block_queue, queued, I2S_AUDIO_BLOCK_COUNT);
  for (size_t i = 0; i < queued_count; ++i)
    reclaim[count++] = (i2s_audio_block_t *)queued[i];
  for (uint32_t i = 0; i < DMA_COUNT; i++) {
    if (active[i]) reclaim[count++] = active[i];
    active[i] = NULL;
  }
  audio_block_queue_reset(&block_queue, rate_hz, enabled);
  low_water_frames = UINT32_MAX;
  critical_section_exit(&lock);
  for (size_t i = 0; i < count; i++) release_block(reclaim[i]);
  if (dma_chan[0] >= 0) restart_dma();
}

void i2s_out_enter_flash_mute(void) {
  i2s_audio_block_t *reclaim[I2S_AUDIO_BLOCK_COUNT + DMA_COUNT];
  void *queued[I2S_AUDIO_BLOCK_COUNT];
  size_t count = 0u;

  irq_set_enabled(DMA_IRQ_0, false);
  for (uint32_t i = 0; i < DMA_COUNT; i++) {
    dma_channel_set_irq0_enabled((uint)dma_chan[i], false);
    dma_channel_abort((uint)dma_chan[i]);
    dma_channel_acknowledge_irq0((uint)dma_chan[i]);
  }

  dma_channel_config cfg = dma_channel_get_default_config((uint)dma_chan[0]);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, false);
  channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, true));
  channel_config_set_chain_to(&cfg, (uint)dma_chan[0]);
  dma_channel_configure((uint)dma_chan[0], &cfg, &pio->txf[sm], silence,
                        dma_encode_endless_transfer_count(), true);

  critical_section_enter_blocking(&lock);
  size_t queued_count = audio_block_queue_drain(&block_queue, queued, I2S_AUDIO_BLOCK_COUNT);
  for (size_t i = 0; i < queued_count; i++)
    reclaim[count++] = (i2s_audio_block_t *)queued[i];
  for (uint32_t i = 0; i < DMA_COUNT; i++) {
    if (active[i]) reclaim[count++] = active[i];
    active[i] = NULL;
  }
  audio_block_queue_reset(&block_queue, rate_hz, false);
  low_water_frames = UINT32_MAX;
  critical_section_exit(&lock);

  for (size_t i = 0; i < count; i++) release_block(reclaim[i]);
}

void i2s_out_exit_flash_mute(bool streaming) {
  dma_channel_abort((uint)dma_chan[0]);
  dma_channel_acknowledge_irq0((uint)dma_chan[0]);
  critical_section_enter_blocking(&lock);
  audio_block_queue_reset(&block_queue, rate_hz, streaming);
  critical_section_exit(&lock);
  restart_dma();
}

uint32_t i2s_out_buffered_frames(void) {
  critical_section_enter_blocking(&lock);
  uint32_t frames = block_queue.buffered_frames;
  critical_section_exit(&lock);
  return frames;
}
uint32_t i2s_out_underrun_frames(void) { return underrun_frames; }
uint32_t i2s_out_low_water_frames(void) {
  return low_water_frames == UINT32_MAX ? 0u : low_water_frames;
}
