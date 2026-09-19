#ifndef AUDIO_CONTROLS_H
#define AUDIO_CONTROLS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CROSSFEED_RECORD_SIZE 8u
#define CROSSFEED_STATE_SIZE 17u
#define OUTPUT_PROCESSING_RECORD_SIZE 8u
#define OUTPUT_PROCESSING_STATE_SIZE 17u
#define OUTPUT_PROCESSING_MONO 0x01u
#define OUTPUT_PROCESSING_SWAP 0x02u
#define OUTPUT_PROCESSING_INVERT_LEFT 0x04u
#define OUTPUT_PROCESSING_INVERT_RIGHT 0x08u
typedef enum {
  CROSSFEED_OFF = 0,
  CROSSFEED_LOW = 1,
  CROSSFEED_MEDIUM = 2,
  CROSSFEED_HIGH = 3,
  CROSSFEED_CUSTOM = 4,
} crossfeed_mode_t;
typedef struct {
  uint8_t mode;
  uint16_t strength_bp, cutoff_hz, delay_us;
} crossfeed_config_t;
typedef struct {
  uint8_t flags;
  int16_t balance_bp;
  uint16_t width_bp;
} output_processing_config_t;
extern crossfeed_config_t const k_crossfeed_default;
extern output_processing_config_t const k_output_processing_default;
bool crossfeed_validate(crossfeed_config_t const *config);
bool crossfeed_equal(crossfeed_config_t const *a, crossfeed_config_t const *b);
void crossfeed_encode(uint8_t *payload, crossfeed_config_t const *config);
bool crossfeed_decode(uint8_t const *payload, size_t length, crossfeed_config_t *config);
bool audio_volume_valid(int16_t volume_q8);
// Master-only Feature Unit: channel=0, mute=1, volume=2. State changes only on success.
bool audio_host_control_set(uint8_t channel, uint8_t selector, uint8_t const *payload,
                            size_t length, int16_t volume[3], int8_t mute[3]);
void audio_host_controls_encode(uint8_t *payload, int16_t const volume[3], int8_t const mute[3]);
void crossfeed_state_encode(uint8_t *payload, crossfeed_config_t const *live,
                            crossfeed_config_t const *saved);
bool output_processing_validate(output_processing_config_t const *config);
bool output_processing_equal(output_processing_config_t const *a,
                             output_processing_config_t const *b);
void output_processing_encode(uint8_t *payload, output_processing_config_t const *config);
bool output_processing_decode(uint8_t const *payload, size_t length,
                              output_processing_config_t *config);
void output_processing_state_encode(uint8_t *payload, output_processing_config_t const *live,
                                    output_processing_config_t const *saved);
void audio_effective_gains(int16_t const volume[3], int8_t const mute[3], float gains[2]);
void audio_controls_init(uint32_t rate, crossfeed_config_t const *crossfeed,
                         output_processing_config_t const *output);
void audio_controls_reset(uint32_t rate);
void audio_controls_set_crossfeed(crossfeed_config_t const *config);
void audio_controls_set_output_processing(output_processing_config_t const *config);
void audio_controls_set_gains(float const gains[2]);
void audio_controls_process(float *left, float *right);
#endif
