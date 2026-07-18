// TinyUSB configuration for the Pumper USB DAC.
//
// This header is included by TinyUSB internals to configure stack behaviour.
// Only USB Device (TUD) mode is enabled; all class drivers except Audio are
// disabled to minimise code size.

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

// --- Hardware / BSP overrides (normally supplied by the board support package) ---

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT 0  // RP2350 has a single USB port on root hub port 0
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED OPT_MODE_FULL_SPEED  // USB Full Speed (12 Mbit/s)
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined  // Set by the CMake target via target_compile_definitions
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE  // Bare-metal (no RTOS)
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0  // Set to 2 for verbose TinyUSB debug output over UART
#endif

// --- TinyUSB device stack settings ---

#define CFG_TUD_ENABLED   1
#define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

// Try to auto-detect the host OS so TinyUSB can apply OS-specific workarounds.
#define CFG_QUIRK_OS_GUESSING 1

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64  // Control endpoint max packet size (bytes)
#endif

// --- Enabled USB class drivers (all others disabled to save flash/RAM) ---

#define CFG_TUD_AUDIO  1  // USB Audio Class 2
#define CFG_TUD_CDC    0
#define CFG_TUD_MSC    0
#define CFG_TUD_HID    1
#define CFG_TUD_MIDI   0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_HID_EP_BUFSIZE 64

// --- UAC2 Audio Function 1 parameters ---

#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX       2       // Stereo (left + right)
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX 2     // 16-bit samples = 2 bytes
#define CFG_TUD_AUDIO_FUNC_1_RESOLUTION_RX       16      // 16 bits used per sample
#define CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE     192000  // Highest advertised sample rate (Hz)
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN            143     // Byte length of the audio function descriptor
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT            1       // Number of Audio Streaming interfaces
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ        64      // Buffer for audio control requests (bytes)

// Maximum OUT endpoint packet size for 192 kHz / 16-bit stereo:
//   ceil(192000 / 1000) + 1 extra packet = 193 frames × 2 ch × 2 bytes = 772 bytes.
//   Rounded up to 776 for alignment.
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX      776

// Software receive buffer: holds ~5 ms of audio at 192 kHz (enough to absorb
// USB scheduling jitter before the data is forwarded to the I2S ring buffer).
#define CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ   8192

// --- Feedback endpoint settings ---

#define CFG_TUD_AUDIO_ENABLE_EP_OUT                    1  // Enable audio OUT (host → device)
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP               1  // Enable SOF feedback for async rate adaptation
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_FORMAT_CORRECTION 0  // No correction needed at Full Speed

#ifdef __cplusplus
}
#endif

#endif
