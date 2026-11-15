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

- `firmware/core/`: platform-independent configuration, DSP, protocol,
  spectrum and storage policy, with Rust integration tests.
- `firmware/Cargo.toml` and `firmware/src/`: root embedded Rust crate/workspace,
  RP2354 hardware, Embassy USB, PIO/DMA and flash.
- `firmware/tools/`: packaging and host diagnostics.
- `firmware/reference/`: frozen compatibility fixtures and validation records.
- `firmware/build.py`: default production Rust build entry point.
- `web/src/`: React/TypeScript WebHID controller.
- `android/app/src/`: Android USB-host controller.
- `external/`: third-party KiCad symbols, footprints, and models.

Do not edit generated directories such as `firmware/build*`, `web/dist`,
`web/node_modules`, or Android build outputs. Preserve unrelated uncommitted
changes; do not reset or rewrite user work.

## Firmware invariants

- Target secure Cortex-M33 (`thumbv8m.main-none-eabihf`) and 2 MiB flash for
  the RP2354, with the pinned Rust 2024 workspace/toolchain.
- Core 0 services Embassy USB and vendor HID. Core 1 performs the EQ/audio-control
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
- I2S packing applies a fixed 0.75 output gain after metering. Preserve this
  hardware-safety ceiling without changing the HID meter or blue LED levels.
- The audio processing order is EQ/preamp, crossfeed, host USB gain/mute,
  global output processing, always-on limiter, and final PCM quantization.
  Output-processing and EQ changes ramp over 10 ms.
- The stereo-linked limiter has immediate attack, a 0 dBFS hard knee, and a
  50 ms release. Signals at or below full scale pass without limiter gain
  reduction. It has no lookahead, true-peak oversampling, dither, or user toggle.
- Keep the advertised format matrix in sync across descriptors, format
  validation, I2S packing, status reporting, and both controllers: 16-bit
  stereo at 44.1/48/88.2/96/176.4/192 kHz, and packed 24-bit stereo at
  44.1/48/88.2/96 kHz only.
- The shared USB clock accepts every supported rate while alternate 0 is idle,
  regardless of the cached previous PCM format. Hosts may set the clock before
  selecting the next alternate. Validate the PCM/rate pair at stream start and
  during active rate changes; defer invalid idle I2S configurations.
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
  changes must be made on both sides: `firmware/core/src/protocol.rs`, firmware
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
- Firmware 3.4 adds GetTelemetry (`0x09`) with a 56-byte schema-1 payload.
  Telemetry keeps core-0 counters separate from the sequence-checked core-1
  DSP snapshot; it must not lock, allocate, divide, or rescan samples in the
  audio path. Stream-scoped values reset on stream start, while transport and
  storage counters are boot-scoped.
- Firmware 3.5 adds opt-in SpectrumStart/Keepalive/Stop/Data (`0x34`–`0x37`).
  Both controllers support this feature while keeping existing commands,
  status/telemetry layouts, and protocol version compatible. Spectrum levels reference post-limiter PCM before I2S gain.
- Firmware 4.0 keeps these commands and layouts compatible. The web controller
  always subscribes to spectrum on supported firmware alongside level meters,
  sharing their start/keepalive/stop lifecycle. Android subscribes while connected
  in the foreground and renders spectrum only on the EQ screen.
- Spectrum capture is a core-1 producer / core-0 consumer FIFO. Only the producer
  publishes the write cursor; only the consumer publishes the read cursor. Reset
  invalidates consumer history and advances a subscription epoch; producer epoch,
  stream generation, and rate are sequence-checked and acknowledged at block end.
  Overflow increments a discontinuity counter and never delays playback.
- Spectrum analysis runs in bounded core-0 steps after USB/audio servicing. Keep
  the 40 us work target and 50 us slice acceptance ceiling; hardware timing still
  needs verification. Capture is integrated in both DSP loops without rescanning.
- Spectrum schema 1 has a 12-byte header: schema, chunk index, chunk count (6),
  level count, little-endian uint32 frame sequence, and uint32 analysis rate
  (44100/48000). Chunks carry 44 levels, except the final 36. Codes 0–192 map
  -96–0 dBFS in half-dB steps. Unsolicited reports use request ID zero. Preserve
  response > meter > spectrum HID priority and immutable frames during chunking.
- The 127-tap decimators use mirrored 128-frame rings to keep symmetric tap
  access contiguous. FFT work advances in small batches independently of
  decimator calls; retain time checks between batches and single-point mapping.
- The 2048-point periodic Hann FFT combines stereo power. Logarithmic points use
  power interpolation for narrow intervals and peak bins for wider intervals;
  256 points do not imply 256 independent low-frequency bands. Regenerate tables
  with `python3 firmware/tools/generate_spectrum_tables.py`.
- `spectrum_diagnostics()` exposes analysis/drop counts, FIFO high-water and worst
  slice time for debugger inspection. Its capture-block time includes the entire
  DSP call, not isolated observer overhead; compare spectrum on/off DSP timing on
  hardware. It deliberately does not change the existing telemetry wire layout.
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
  interfere with USB servicing.
- Preserve device diagnostics for sample rate, bit depth, stream state,
  approximate temperature, clock, worst DSP block time, underruns, and I2S
  low-water mark.

### Dependency boundary

The firmware uses the pinned Rust dependencies and narrowly documented Embassy
patches. Do not add Pico SDK, TinyUSB or C application dependencies. Leave any
external Pico SDK checkout untouched; it is not part of the active codebase.

## Web controller rules

- Desktop is the web controller’s design and verification target. Prioritize
  mouse and keyboard use, compact EQ tables, and desktop screenshots. Narrow
  desktop windows may scroll the table; do not switch bands to mobile cards
  or spend design work on mobile-device layouts.

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
- An acknowledged SpectrumStart begins a fresh frame-sequence namespace even
  within the same connection. Reset web reassembly and animation at that report
  boundary; stale display expiry alone must retain replay protection.
- Request meter reports every 20 ms; illuminate segments immediately and fade
  extinguished segments over 50 ms.
- Animate the web spectrum on browser animation frames, independently of HID
  report cadence. Retarget immediately to the latest complete frame; use dB-height
  smoothing (8 ms attack, 70 ms release) and cancel animation on hidden pages,
  disconnect, unsupported firmware, unmount, and stale data. There is no spectrum
  toggle or browser preference; visibility only pauses rendering. Never queue old visual frames.
- WebHID works on secure origins and `http://localhost`; no firmware
  development-origin flag is required.
- Do not add or run Playwright unless explicitly requested. Use Vitest, the
  production build, and manual browser verification.

### Web localization

- UI messages use the typed `t()` catalog in `web/src/i18n.ts` and
  `web/src/locales/zh-TW.json`. English source messages are the keys and fallback.
- Resolve browser language preferences once per page load. Do not add a language
  selector, URL override, or persisted app-language preference.
- Use natural Taiwan wording (for example 位準, 等化器, 取樣率), consistent
  Chinese/English spacing, and full-sentence rewrites when terminology changes.
  Do not mechanically substitute terms inside sentences without rereading them.
- Keep familiar English filter names in Traditional Chinese and explain their
  audible effect in plain Chinese. Explain Q, BW, shelf steepness, and other
  audio controls instead of relying on literal technical translations.
- Translate complete messages with named placeholders, including accessible
  labels, validation errors, and diagnostics. Preserve EQ exchange-format
  keywords, numeric serialization, HID payloads, and internal state tokens.

## Android controller rules

- The Android app targets `minSdk 34` and uses JDK 17, Gradle, Kotlin, and
  Jetpack Compose/Material 3. Keep debug-only simulator code out of release
  builds.
- Set the Android `versionName` to `1.<firmware major>.<firmware minor>` for
  the newest firmware version it supports (currently `1.4.1` for firmware
  4.1). Update it when supported firmware changes, and increment `versionCode`
  for every release.
- Use the same fixed-size HID protocol and persistence semantics as the web
  controller. Crossfeed, output processing, and EQ profile saves remain
  independent operations.
- Keep the main EQ graph display-only. The band editor shows its own filter
  response without global preamp. EQ text import/export uses the web format;
  imports validate before replacing preview and never save implicitly.
- Spectrum uses schema-1 reassembly with replay protection, a fresh sequence
  namespace at acknowledged start, and a 500 ms stale display timeout. Animate
  dB height on Compose frames with 8 ms attack / 70 ms release. Stop rendering
  when the EQ screen leaves composition and clear data on disconnect.
- Live previews must not write flash. The simulator should exercise the real
  report codec and controller state, but it cannot stand in for Android USB
  permission, HID discovery, or physical-device tests.

## Build and verify

Run the default production firmware build from the repository root:

```sh
python3 firmware/build.py
```

Run Rust verification after firmware changes:

```sh
cd firmware
cargo test --locked
cargo test --locked -p embassy-usb --lib
cargo fmt --all -- --check
cargo clippy --locked --all-targets -- -D warnings
cargo clippy --locked -p pumper-device --release \
  --target thumbv8m.main-none-eabihf --features diagnostics -- -D warnings
python3 -m unittest discover -s tools -p 'test_*.py'
```

Compatibility tests use frozen outputs in `reference/golden/`. Do not regenerate
expected values from the Rust implementation being tested. Preserve the captured
source/fixture hashes, corpus coverage, and original audio tolerances. Do not
restore C sources, build files or compiler-driven oracle adapters as runtime or
test dependencies.

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

### USB volume pinning (firmware 4.1)

- Output-processing flag `0x10` pins USB gain to unity while honoring mute.
  Preserve host requested volume for USB/HID readback and restoring follow mode.
- Follow USB volume defaults on; pinning previews through the 10 ms gain ramp
  and persists only through SaveOutputProcessing, using flash schema 5. Older
  banks migrate with pinning off and without unsolicited writes.
- Controllers gate the toggle at 4.1 and show `Pinned` from acknowledged state.
  The output record size and protocol/status/telemetry layouts remain unchanged.

### Factory reset (firmware 4.2)

- FactoryReset (`0x42`) takes an empty payload and erases/verifies the entire final 8 KiB settings region through the deferred flash-safe path. Success has an empty response before delayed reboot; failure returns storage error without reboot. No replacement bank is written.
- Both controllers gate the destructive confirmation action at 4.2. Android version is 1.4.2 (code 7). Interrupted erasure is not atomic.

### Rust firmware ownership and validation

- Rust is the default and sole firmware implementation. Frozen reference outputs
  preserve compatibility coverage; previously exported C UF2s are rollback artifacts.
- Platform tests remain skipped/untested. Default selection
  does not convert any missing hardware check into a pass; preserve those gaps
  and historical build identities in the validation record.
- The transport prototype reported 0.0; the complete command/DSP implementation
  reports 5.0. Artifact names and diagnostic build identity distinguish the Rust
  candidate. Do not add prototype handling to the existing controllers.
- The 80% processing-load limit is waived. Retain historical load, underrun and
  spectrum timing measurements; a waived load limit is not evidence of zero
  underruns or cross-host acceptance. Do not expand optimization work beyond
  the requested scope.
- Cortex-M33 secure target, 2 MiB flash, final 8 KiB reserved for settings. Program
  images must exclude that reservation. The flash bootstrap
  must relocate runtime code, constants, and vectors before calling Rust Reset.
  Run the ELF/UF2 checks, not just the linker, before hardware tests.
- Six audio buffers move by unique mutable-reference tokens through SPSC queues.
  Core 0 produces ready blocks; core 1 returns completed/aborted blocks. Never
  return or mutate a buffer while DMA still owns it. Core 1 alone owns PIO0,
  GPIO19–21, and DMA0/1; its current transport loop polls chained DMA completion.
- Stream configuration and telemetry payloads use atomic sequence-checked fields.
  Full DSP configurations move as owned values through a fixed SPSC channel;
  queue slots cannot be reused until the reader releases them. Never substitute
  racing non-atomic snapshot copies, even with sequence retries.
- Core 0 reserves 128 KiB of stack; core 1 owns a separate 32 KiB stack. Check
  both ELF bounds and diagnostic watermarks after increasing processing state.
- Flash jobs transfer an aligned image token to core 1. Quiesce DSP and play
  RAM-backed DMA silence while core 0 serves USB from RAM. Flush XIP cache after
  ROM writes before verifying data pages; commit the header only after all data
  pages verify. Only the completion token allows core 0 to read flash again.
- Rust dependency patches are limited to the vendored Embassy files documented
  in `firmware/vendor/PATCHES.md`; keep upstream licenses and versions.
- Neither firmware nor host compatibility tests compile C. The embedded image
  must not link Pico SDK, TinyUSB or C application code. Keep public build/recovery
  instructions in the README and incomplete hardware acceptance explicit.
- Diagnostics are compile-time opt-in, read-only HID Feature reports. Production
  descriptors/handlers must not expose them. Do not reuse production telemetry
  reserved fields for diagnostics.
- If device testing leaves Pumper inaccessible and requires physical BOOTSEL,
  stop device testing until physical access is available. Do not attempt blind
  recovery resets or continue flashing.
- Restart through the RP2354 ROM watchdog reboot API, after the HID transfer
  completes and the acknowledgement delay elapses. Do not use ARM SYSRESETREQ:
  the prototype's hardware restart test left USB unresponsive with that path.

- USB suspend pauses the selected alternate without discarding it. Resume restores
  it; bus reset or unconfigure cancels that restoration. Preserve HID subscription
  lease handling independently from a temporary suspend.
- Hardware checks that claim audio interfaces must not run concurrently with
  playback/format tests or firmware deployment. The basic HID diagnostics utility
  uses the operating system's HID driver without detaching audio drivers.

- Use `firmware/tools/build_firmware.py` for reproducible firmware artifacts;
  it remaps workspace, Cargo-cache and target paths. Direct development builds
  can contain machine-specific panic source paths. Keep build identity inputs
  and artifact hashes distinct from hardware acceptance evidence.
- Match C HID admission: count/drop busy reports before parsing; malformed
  envelopes are counted/dropped without a response. Valid envelopes with invalid
  commands or payloads receive protocol errors. Reject new work while a flash
  operation or acknowledged reset is pending, and discard queued work on reset.

- While USB is active, never switch SYS through the 12 MHz reference (RP2350-E12).
  Keep PLL_SYS at 220 MHz and change only the 16.16 SYS divider: 65536 for
  active PCM16/192 kHz, 80100 for other streaming (179,999,001 Hz), and 96119
  idle (150,000,728 Hz). Keep 1.2 V for fractional-divider fast cycles;
  USB/ADC/timer clocks remain independent. Update cached SYS/PERI, not PLL_SYS.
- Serialize hardware tests and preserve failure evidence. A USB timeout is a
  failure, never a substitute for a requested STALL. Keep only build/packaging,
  coefficient generation and basic HID utilities in the shipped tools directory;
  use temporary scripts for one-off investigations.

- Loaded DSP/spectrum stress must use nonzero PCM and an unmuted host path;
  silence skips spectrum logarithms and is only a transport workload. Preserve
  the actual run duration and failures. An early stop is not a completed hour.

- Keep prior clock-policy measurements as historical evidence. Spectrum work
  should preserve the 2048-point FFT, 44.1/48 kHz analysis rates and numeric
  tolerances unless a change is explicitly justified. Listening feedback does
  not establish instrumented electrical I2S timing.

- Spectrum must keep making bounded progress while HID IN completion is pending.
  Keep the same transfer future alive, prioritize its completion, and yield to
  USB/audio between analysis slices. Never modify an in-flight report or select
  another report until it completes; preserve response > meter > spectrum.
- FFT batches may not cross analysis-stage boundaries. In particular, entering
  level mapping from a power-bin batch must not execute multiple mapped points
  before checking time. Keep the original cadence threshold and numeric oracle.

- Firmware is a Cargo workspace rooted at `firmware/`; the embedded package is
  the workspace root (`src/`, `build.rs`, linker scripts), with `core/` and
  `tools/` members. Do not recreate a nested `firmware/rust/` migration tree.
  Existing C build caches were preserved under ignored `pumper-backups/`.

- Firmware release 5.0 identifies the Rust implementation. Keep the HID wire
  protocol at version 1, existing command/status layouts and flash schema 5.
  Workspace package version and the reported firmware major/minor must agree.

- Limiter release must converge to exact unity at every supported sample rate,
  including after silence following clipping. Float rounding can stall release
  below the unity threshold at high rates; detect stalled recovery only when
  the instantaneous target is unity. Never clear limiting on a steady overload.
- Patch releases use Cargo's patch component; the existing two-byte HID firmware
  version reports major/minor only. Do not change the wire layout to add a patch.

- Refresh LEDs on an independent 1 ms timer, never inside the HID transmit loop.
  Closing a host HID handle can leave IN pending indefinitely while USB audio
  continues. Preserve the in-flight transfer and turn both LEDs off when idle.

- HID IN backpressure must not stall host-control publication, subscription
  expiry, meter draining or stream housekeeping. Poll the same transfer until
  completion and run bounded background work with a timer even when spectrum
  is inactive. Keep command dispatch and report selection serialized.
- Route UAC entity controls only for interface recipients addressing
  AudioControl interface 0 and master channel 0. Reject other addresses before
  reading or changing clock, volume or mute state.
