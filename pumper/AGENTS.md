# Pumper Agent Guide

## Documentation and repository hygiene

README files are public project documentation. Keep them focused on what
Pumper is, supported user-facing features, prerequisites, and how to build,
run, test, flash, or deploy each part. Put agent-only constraints,
implementation invariants, protocol details, and review reminders here.

Pumper is an RP2354-based USB Audio Class 2 DAC with a ten-band parametric EQ.
The firmware also exposes a vendor HID interface used by the React WebHID and
native Android controllers.

Repository layout:

- `firmware/src/`: RP2354 firmware, USB descriptors, DSP, I2S, HID protocol,
  audio controls, and profile storage.
- `firmware/tests/`: portable host tests for audio formats, DSP, protocol,
  activity, queues, and simulated flash storage.
- `web/src/`: React/TypeScript WebHID controller.
- `android/app/src/`: Android USB-host controller.
- `external/`: third-party KiCad symbols, footprints, and models.

Do not edit generated directories such as `firmware/build*`, `web/dist`,
`web/node_modules`, or Android build outputs. Preserve unrelated uncommitted
changes; do not reset or rewrite user work.

## Firmware invariants

- Target `PICO_BOARD=pico2`, `PICO_PLATFORM=rp2350-arm-s`, and 2 MiB flash for
  the RP2354.
- Core 0 services TinyUSB and vendor HID. Core 1 performs the EQ/audio-control
  chain and submits audio to I2S. Never block the USB task or allocate in the
  audio path.
- Audio moves through a fixed pool of aligned blocks from USB decoding on core
  0 to DSP on core 1 and then to ping-pong PIO/DMA. Preserve the startup
  priming and silence-on-underrun behavior.
- Publish configuration through immutable or sequence-checked snapshots.
  Meter accumulation may not hold a spinlock in the audio path.
- The HID meter reports both pre-EQ input levels and the final post-processing
  output levels. The blue LED uses the saturated post-processing output peak
  immediately before I2S packing, without scanning the samples a second time.
- The audio processing order is EQ/preamp, crossfeed, host USB gain/mute,
  global output processing, always-on limiter, and final PCM quantization.
  Output-processing and EQ changes ramp over 10 ms.
- The stereo-linked limiter has immediate attack, a -1 dBFS soft knee, and a
  50 ms release. It has no lookahead, true-peak oversampling, dither, or user
  toggle.
- Keep the advertised format matrix in sync across descriptors, format
  validation, I2S packing, status reporting, and both controllers: 16-bit
  stereo at 44.1/48/88.2/96/176.4/192 kHz, and packed 24-bit stereo at
  44.1/48/88.2/96 kHz only.
- The ten EQ bands support RBJ low-pass, high-pass, notch, and constant-0-dB
  peak band-pass filters in addition to the gain filters. The extra filter
  types use frequency and Q; gain and bandwidth mode are invalid for them.

### Profile and flash storage

- The final 8 KiB of flash is reserved for two alternating 4 KiB profile
  banks. Preserve the post-build size check.
- Data pages must be programmed and validated before a bank commit header so a
  power loss leaves the previous complete bank available. Keep CRC validation
  and generation-based bank selection intact.
- Profile save, profile selection, and power-on default selection are separate
  operations. Flash writes happen only for explicit save/default/delete
  actions; live edits and loading a profile do not write flash.
- Deleting the default promotes the lowest-numbered remaining profile.
  Deleting the last profile restores the flat compiled fallback on the next
  boot. The first saved profile becomes the default when no profile exists.
- Preserve migration of the legacy single-profile record into Profile 1 and
  schema migration of older banks without an unsolicited flash write.
- Crossfeed and output-processing saved settings are global and independent of
  EQ profiles. Their preview state must remain volatile until its explicit
  save action.

### Firmware protocol and status

- HID reports are fixed at 64 bytes and use protocol version 1. Protocol
  changes must be made on both sides: `firmware/src/eq_protocol.h`, firmware
  command handling, `web/src/protocol.ts`, and the Android protocol codec.
- Bump the firmware minor version when adding commands or changing payload
  semantics. Preserve backward-compatible decoding where the controllers
  already support older firmware.
- Firmware 3.0 commands include Get/Set/SaveOutputProcessing (`0x08`, `0x13`,
  and `0x27`). The 8-byte output record contains flags, signed balance basis
  points, width basis points, and zeroed reserved fields. State responses
  contain live config, saved config, and dirty state.
- Firmware 3.1 status is 48 bytes. Byte 44 reports active PCM bit depth (16 or
  24); bytes 45–47 are reserved and zeroed. Do not move existing fields.
- Host USB volume and mute are controlled by the audio host. The controllers
  display them but must not treat them as profile settings.

### LEDs and device behavior

- The red LED is active-low, steady while streaming, and off while idle. While
  streaming, received HID output reports—including malformed or rejected
  reports—and completed HID input transfers trigger an inverted 25 ms activity
  pulse followed by at least 25 ms at baseline. Coalesce additional traffic
  into one pending pulse and reset activity on USB unmount.
- `LED_PWM_ON_LEVEL` is the maximum brightness for both the fixed LED state and
  the blue audio meter. Preserve active-low PWM behavior and the brightness
  cap.
- The non-blocking core-0 LED task must tolerate timer wraparound and must not
  interfere with TinyUSB servicing.
- Preserve device diagnostics for sample rate, bit depth, stream state,
  approximate temperature, clock, worst DSP block time, underruns, and I2S
  low-water mark.

### SDK dependency boundary

Keep Pico SDK/TinyUSB fixes outside this repository. The currently verified
TinyUSB fixes are commits `86c28b76f` and `1ec93757f` in the SDK submodule.
Do not vendor or commit the patched SDK into Pumper.

## Web controller rules

- Use React, TypeScript, Tailwind CSS 4, daisyUI, and Lucide icons. Keep the UI
  utility-first with dark `stone` surfaces where the existing design calls for
  them.
- `web/src/styles.css` should contain only the Tailwind import and genuinely
  global browser defaults.
- Use Lucide icons for command buttons. Do not add hand-drawn icon SVGs. The
  EQ graph remains a data visualization and may use SVG directly.
- Preserve live preview behavior: control edits update the DAC without writing
  flash. Coalesce preview updates and discard stale queued work after
  disconnect/reconnect.
- An empty profile is a save target, not an unsaved EQ edit. Only actual EQ
  edits should trigger the discard confirmation.
- Crossfeed, output processing, and EQ profiles have independent saved/dirty
  state. Reconnecting reads device state instead of replaying stale browser
  edits.
- WebHID works on secure origins and `http://localhost`; no firmware
  development-origin flag is required.
- Do not add or run Playwright unless explicitly requested. Use Vitest, the
  production build, and manual browser verification.

## Android controller rules

- The Android app targets `minSdk 34` and uses JDK 17, Gradle, Kotlin, and
  Jetpack Compose/Material 3. Keep debug-only simulator code out of release
  builds.
- Use the same fixed-size HID protocol and persistence semantics as the web
  controller. Crossfeed, output processing, and EQ profile saves remain
  independent operations.
- Live previews must not write flash. The simulator should exercise the real
  report codec and controller state, but it cannot stand in for Android USB
  permission, HID discovery, or physical-device tests.

## Build and verify

Run firmware commands from the repository root:

```sh
export PICO_SDK_PATH="Path to Pico SDK"
cmake -S firmware -B firmware/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_BOARD=pico2 \
  -DPICO_PLATFORM=rp2350-arm-s \
  -DPICO_FLASH_SIZE_BYTES=2097152
cmake --build firmware/build --target rp2350_usb_dac -j
```

Run portable firmware tests after DSP, protocol, format, or storage changes:

```sh
cmake -S firmware/tests -B /tmp/pumper-firmware-tests
cmake --build /tmp/pumper-firmware-tests -j
ctest --test-dir /tmp/pumper-firmware-tests --output-on-failure
```

Run frontend verification from `web/`:

```sh
pnpm test
pnpm build
```

Run Android verification from `android/`:

```sh
./gradlew testDebugUnitTest lintDebug assembleDebug
```

Before handing off, run `git diff --check` and report any verification that
could not be performed.
