# Pumper firmware

Pumper's default firmware is written in Rust for the RP2354 secure Cortex-M33,
with 2 MiB flash. It exposes USB Audio Class 2 and vendor HID interfaces for the
existing React WebHID and Android controllers.

Firmware 5.0 provides ten-band parametric EQ, crossfeed, host volume/mute and
volume pinning, balance/mono/width controls, a stereo limiter, meters, live
spectrum, persistent profiles, explicit saves, factory reset, restart, and BOOTSEL.
Version 5.0 identifies the Rust firmware; HID protocol version 1 and saved-profile
compatibility are preserved. Patch 5.0.3 keeps host volume/mute controls responsive after the controller
disconnects and includes the earlier LED and limiter fixes. The two-component
HID version field continues to display 5.0.
Live edits do not save settings until the corresponding save action is used.
Factory reset erases all saved profiles and global processing settings.

| Sample depth | Supported sample rates (kHz) |
| --- | --- |
| 16-bit stereo | 44.1, 48, 88.2, 96, 176.4, 192 |
| Packed 24-bit stereo | 44.1, 48, 88.2, 96 |

PCM16/192 kHz uses a 220 MHz clock boost. Other formats run at approximately
180 MHz, and idle operation at approximately 150 MHz. Spectrum displays processed
digital audio before the fixed I2S output gain; it does not measure analog output.

Linux playback and HID checks have passed, but validation is incomplete.
Windows, macOS, physical Android, instrumented I2S measurements, and some
long-duration and recovery checks remain unperformed.

## Source layout

`firmware/` is the Cargo workspace and embedded firmware crate:

- `Cargo.toml`, `src/`, `build.rs`, `memory.x`, `ram.x`: RP2354 firmware.
- `core/`: portable Rust processing, protocol and storage library, with tests.
- `tools/`: Rust UF2 packager and Python build/device utilities.
- `reference/`: frozen compatibility fixtures and validation records.
- `vendor/`: pinned Embassy patches.

## Requirements

- Rustup; [rust-toolchain.toml](rust-toolchain.toml) pins Rust 1.94.1
  and the `thumbv8m.main-none-eabihf` target.
- Python 3 for building and packaging.
- The normal native linker required by Rust host tools and tests.
- Optional: Python `hidapi` or a system hidapi library for diagnostics, and
  picotool with RP2350 support for USB flashing and backups.

The Rust firmware build does not require Pico SDK, TinyUSB, or an Arm C compiler.

## Build

From the repository root:

```sh
python3 firmware/build.py
```

For an ELF-only development build, run `cargo firmware --locked` from
`firmware/`.

The production image is `firmware/artifacts/pumper-rust-5.0.3.uf2`.
The builder pins dependencies, checks RAM/flash placement, reserves the final
8 KiB for settings, and writes a BIN and `.build.json` manifest alongside the UF2.
It does not flash a device.

To choose an output file or build test-only diagnostics:

```sh
python3 firmware/build.py --output /tmp/pumper-rust.uf2
python3 firmware/build.py --diagnostics \
  --output firmware/artifacts/pumper-rust-5.0.3-diagnostics.uf2
```

See [TESTING.md](TESTING.md) for detailed build options and device probes.

## Test

```sh
cd firmware
cargo test --locked
cargo test --locked -p embassy-usb --lib
cargo fmt --all -- --check
cargo clippy --locked --all-targets -- -D warnings
python3 -m unittest discover -s tools -p 'test_*.py'
```

Rust tests compare codecs, DSP, spectrum and storage against frozen compatibility
fixtures captured from the previous firmware, and exercise Rust state machines
and simulated flash failures. They do not compile any C source.

## Flash and recover

1. Request BOOTSEL from the controller, or hold the board's BOOTSEL button while
   reconnecting USB.
2. Copy `firmware/artifacts/pumper-rust-5.0.3.uf2` to the BOOTSEL drive.
3. Wait for the drive to disconnect and Pumper to restart.

Program UF2s leave saved profiles and global settings intact. Keep a known-working
program UF2 for rollback. To restore the previous C firmware, enter BOOTSEL and
copy its UF2 instead; do not erase settings or restore a full-flash backup merely
to change firmware implementations. If software BOOTSEL is unavailable, use the
physical button.
