// I2S output driver for the RP2350.
//
// Audio data received over USB is written into a ring buffer by the main task
// and continuously consumed by a DMA channel that feeds the PIO state machine.
// The PIO program (audio_i2s.pio) serialises 32-bit I2S words per channel
// (64 BCLKs per stereo frame) onto the I2S bus (BCLK / LRCK / DIN).
//
// Data flow:
//   USB audio callback → i2s_out_write_stereo16()
//       → s_ring[] (ring buffer, I2S_RING_WORDS entries)
//           → i2s_dma_irq_handler() (fires when a DMA chunk finishes)
//               → s_dma_chunk[] (flat DMA source buffer)
//                   → PIO TX FIFO → I2S pins
//
// Thread safety: the ring buffer head/tail are only modified while interrupts
// are disabled (save_and_disable_interrupts / restore_interrupts), so the ISR
// and the main task can share the ring safely.

#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "audio_i2s.pio.h"

// GPIO pin numbers for the I2S bus.
#define I2S_DIN_PIN   19  // Serial data output
#define I2S_LRCK_PIN  20  // Frame sync (FSYNC / LRCK) output
#define I2S_BCLK_PIN  21  // Bit clock output (must be I2S_LRCK_PIN + 1)

// Ring buffer capacity in stereo frames.
// 8192 frames is enough for ~42 ms at 192 kHz, preventing underruns during
// USB scheduling jitter.
#define I2S_RING_FRAMES 8192

// Number of frames copied from the ring buffer into the flat DMA source buffer
// on each DMA completion interrupt.  192 frames @ 48 kHz ≈ 4 ms per chunk.
#define I2S_DMA_CHUNK_FRAMES 192
#define I2S_RING_WORDS (I2S_RING_FRAMES * 2u)
#define I2S_DMA_CHUNK_WORDS (I2S_DMA_CHUNK_FRAMES * 2u)

static PIO s_pio = pio0;    // PIO instance used for I2S
static uint s_sm = 0;       // State machine index within s_pio
static int s_dma_chan = -1; // DMA channel claimed for I2S output

// Lock-free power-of-two ring buffer shared between the main task (writer) and
// the DMA IRQ handler (reader).
// Each entry is one 32-bit I2S channel word in wire order: [right0, left0, ...].
// 16-bit PCM samples are expanded to 32-bit words by left-shifting 16.
static volatile uint32_t s_ring[I2S_RING_WORDS];
static volatile uint32_t s_head = 0;  // Write word index (advanced by main task)
static volatile uint32_t s_tail = 0;  // Read  word index (advanced by DMA ISR)
static volatile uint32_t s_rate_hz = 48000; // Current sample rate in Hz

// Flat buffer that DMA reads from; refreshed from the ring buffer by the ISR.
static uint32_t s_dma_chunk[I2S_DMA_CHUNK_WORDS];

// Returns the number of words currently stored in the ring buffer.
static inline uint32_t ring_words_count(void) {
  return (s_head - s_tail) & (I2S_RING_WORDS - 1u);
}

// Returns the number of additional words that can be written before the ring
// buffer is full (one slot is kept empty to distinguish full from empty).
static inline uint32_t ring_words_free(void) {
  return (I2S_RING_WORDS - 1u) - ring_words_count();
}

// Copies up to I2S_DMA_CHUNK_WORDS words from the ring buffer into
// s_dma_chunk, padding with silence (0) when the ring buffer is empty.
// Called only from the DMA IRQ handler so no additional locking is needed.
static void i2s_refill_dma_chunk(void) {
  for (uint32_t i = 0; i < I2S_DMA_CHUNK_WORDS; i++) {
    if (s_tail != s_head) {
      s_dma_chunk[i] = s_ring[s_tail];
      s_tail = (s_tail + 1u) & (I2S_RING_WORDS - 1u);
    } else {
      // Ring buffer underrun: output silence instead of stale data.
      s_dma_chunk[i] = 0;
    }
  }
}

// Refills s_dma_chunk and kicks off the next DMA transfer to the PIO TX FIFO.
static void i2s_start_dma_transfer(void) {
  i2s_refill_dma_chunk();
  dma_channel_set_read_addr(s_dma_chan, s_dma_chunk, false);
  dma_channel_set_trans_count(s_dma_chan, I2S_DMA_CHUNK_WORDS, true);
}

// DMA completion ISR — fires on DMA_IRQ_0 when the current chunk finishes.
// Immediately starts the next transfer so the PIO FIFO never empties.
void __isr i2s_dma_irq_handler(void) {
  if (!dma_channel_get_irq0_status(s_dma_chan)) {
    // Spurious interrupt from a different channel — ignore.
    return;
  }
  dma_channel_acknowledge_irq0(s_dma_chan);
  i2s_start_dma_transfer();
}

// Reconfigure the PIO state machine clock divider so that the bit clock
// matches the new sample rate.
//
// The PIO program uses 128 SM cycles per stereo frame
// (2 cycles/bit × 64 bits, i.e. 32-bit slots per channel).
// Therefore:  SM frequency = sample_rate_hz × 128
//
// The RP2350 clock divider is an 8.8 fixed-point number:
//   div = clk_sys / SM_freq = clk_sys / (sample_rate × 128)
// Multiplying numerator and denominator gives the 8.8 value directly:
//   div_8_8 = (clk_sys × 2) / sample_rate_hz
void i2s_out_set_sample_rate(uint32_t sample_rate_hz) {
  if (sample_rate_hz == 0) {
    return;
  }
  s_rate_hz = sample_rate_hz;
  // PIO consumes 128 state-machine cycles per stereo frame:
  // 2 cycles per bit (out + jmp/set) × 64 bits = 128 cycles.
  // SM frequency = sample_rate × 128.
  // 8.8 fixed-point: div = clk_sys × 2 / sample_rate
  // (equivalent to clk_sys × 256 / (sample_rate × 128))
  uint32_t clk_hz = clock_get_hz(clk_sys);
  uint32_t div_8_8 = clk_hz * 2u / sample_rate_hz;
  pio_sm_set_clkdiv_int_frac(s_pio, s_sm, div_8_8 >> 8u, div_8_8 & 0xffu);
}

// Initialise the I2S output subsystem.
//
// Configures GPIO pins, loads the PIO program, claims a DMA channel, sets up
// the DMA completion interrupt, and starts continuous playback (initially
// silence until audio data is written into the ring buffer).
void i2s_out_init(uint32_t sample_rate_hz) {
  // Route the three I2S GPIO pins to PIO0.
  gpio_set_function(I2S_DIN_PIN, GPIO_FUNC_PIO0);
  gpio_set_function(I2S_LRCK_PIN, GPIO_FUNC_PIO0);
  gpio_set_function(I2S_BCLK_PIN, GPIO_FUNC_PIO0);

  // Load the PIO program and initialise the state machine.
  uint offset = pio_add_program(s_pio, &audio_i2s_program);
  audio_i2s_program_init(s_pio, s_sm, offset, I2S_DIN_PIN, I2S_LRCK_PIN);
  i2s_out_set_sample_rate(sample_rate_hz);

  // Claim a free DMA channel for streaming data from s_dma_chunk to the PIO TX FIFO.
  s_dma_chan = dma_claim_unused_channel(true);
  dma_channel_config cfg = dma_channel_get_default_config(s_dma_chan);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32); // 32-bit transfers (one I2S channel word per transfer)
  channel_config_set_read_increment(&cfg, true);            // Walk forward through s_dma_chunk
  channel_config_set_write_increment(&cfg, false);          // Always write to the same PIO TX FIFO register
  // Pace transfers to the PIO TX FIFO's available space (DREQ = data request signal).
  channel_config_set_dreq(&cfg, pio_get_dreq(s_pio, s_sm, true));

  dma_channel_configure(
      s_dma_chan,
      &cfg,
      &s_pio->txf[s_sm], // Destination: PIO state machine TX FIFO
      s_dma_chunk,        // Source: flat chunk buffer
      I2S_DMA_CHUNK_WORDS,
      false);             // Don't start yet — started below after IRQ is set up

  // Register the DMA completion interrupt so we can reload the next chunk.
  irq_set_exclusive_handler(DMA_IRQ_0, i2s_dma_irq_handler);
  dma_channel_set_irq0_enabled(s_dma_chan, true);
  irq_set_enabled(DMA_IRQ_0, true);

  // Enable the PIO state machine and start the first DMA transfer.
  pio_sm_set_enabled(s_pio, s_sm, true);
  i2s_start_dma_transfer();
}

// Write interleaved stereo 16-bit PCM samples into the ring buffer.
//
// @param interleaved  Pointer to array of int16_t pairs: [L0, R0, L1, R1, ...]
// @param frame_count  Number of stereo frames to write (each frame = 2 samples)
// @return             Number of frames actually written (may be less than
//                     frame_count if the ring buffer is nearly full)
//
// Each 16-bit sample is expanded into one 32-bit I2S channel word:
//   word = (int32_t)sample << 16
// so the sample MSB is transmitted first and each channel slot stays 32 bits.
size_t i2s_out_write_stereo16(const int16_t *interleaved, size_t frame_count) {
  uint32_t save = save_and_disable_interrupts();
  size_t free_words = ring_words_free();
  size_t can_write = free_words / 2u;
  if (can_write > frame_count) {
    can_write = frame_count;
  }
  for (size_t i = 0; i < can_write; i++) {
    int32_t left = interleaved[i * 2u];
    int32_t right = interleaved[i * 2u + 1u];
    // PIO emits LRCK=1 slot first, then LRCK=0 slot, so queue right then left.
    s_ring[s_head] = (uint32_t) (right << 16u);
    s_head = (s_head + 1u) & (I2S_RING_WORDS - 1u);
    s_ring[s_head] = (uint32_t) (left << 16u);
    s_head = (s_head + 1u) & (I2S_RING_WORDS - 1u);
  }
  restore_interrupts(save);
  return can_write;
}

// Returns the number of stereo frames that can be written without blocking.
// Interrupts are briefly disabled to get a consistent snapshot of head/tail.
size_t i2s_out_free_frames(void) {
  uint32_t save = save_and_disable_interrupts();
  size_t free_frames = ring_words_free() / 2u;
  restore_interrupts(save);
  return free_frames;
}
