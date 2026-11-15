# Firmware testing and recovery

This is the default Rust implementation of Pumper firmware 5.1. It supports
the existing USB format matrix, ten-band EQ, crossfeed, host volume/mute and
pinning, output processing, limiter, meters, spectrum, profiles, explicit saves,
factory reset, restart, and BOOTSEL. The React and Android controllers use the
same protocol and remain unchanged.

Build and flash instructions are also available in [README.md](README.md).
Hardware validation remains incomplete. PCM16/192 kHz uses a 220 MHz boost and
has limited processing headroom. Long-duration validation of the current build
is still required. Windows, macOS, physical Android, and instrumented electrical
I2S validation remain outstanding.

## Prerequisites

- Rustup; the workspace pins Rust 1.94.1 and the Cortex-M33 target.
- Python 3 for packaging and HID diagnostics.
- The normal native linker required by Rust host tools and tests.
- The Python `hidapi` package for the HID probe, or a native hidapi library on Linux.
- Optional: picotool 2.3.0 with USB support for loading and recovery.

Neither the firmware nor its compatibility tests compile C source or require
Pico SDK. Rust host tools still need their normal platform linker.

## Build and verify

From this directory:

```sh
cargo test --locked
cargo test --locked -p embassy-usb --lib
cargo fmt --all -- --check
cargo clippy --locked --all-targets -- -D warnings
python3 tools/build_firmware.py \
  --output artifacts/pumper-rust-5.1.0.uf2
cargo clippy --locked -p pumper-device --release \
  --target thumbv8m.main-none-eabihf --features diagnostics -- -D warnings
```

Packaging checks the secure boot block, SRAM placement, stack reservation,
overlapping load segments, and the final 8 KiB settings reservation. It produces
a UF2 and a BIN. It does not flash a device. Production builds omit the test
Feature report. Add `--diagnostics` and use a distinct output filename to build
the read-only test interface.

The build script uses the pinned toolchain and lockfile, remaps machine-specific
source/cache paths, and writes a `.build.json` artifact manifest. Add `--offline`
after dependencies are cached. `--target-dir` selects a separate build directory.
Direct `cargo firmware --locked` remains available for development, but use the
script when comparing reproducible UF2 hashes across machines.

The 64-byte test-only HID Feature report (`PDBG`, schema 4) provides a build
identifier, total audio-block time, spectrum slice time and drop counts, FIFO
high-water mark, stack headroom, ownership faults, USB reset count, underruns,
clock, completed analysis frames, and the last flash result. The production
build omits this descriptor and handler.

```sh
python3 tools/hid_probe.py status
python3 tools/hid_probe.py diagnostics
```

Linux needs permission to access the DAC's hidraw device. Add `--serial` to select
a particular DAC when multiple units are attached.

For Windows and macOS, install the [Python hidapi binding](https://github.com/trezor/cython-hidapi)
in your Python environment with `python -m pip install hidapi`. The probe also
works with a system hidapi library when the Python binding is absent. It uses
the OS HID driver and does not require changing the USB audio driver.

## Playback and controller checks

Connect a DAC running the firmware to the host being tested and select **Pumper USB DAC** as the audio output. Set formats
using the host's sound format controls (Audio MIDI Setup on macOS); confirm the
actual wire format with the probe while audio is playing.

| Sample depth | Supported rates (kHz) |
| --- | --- |
| 16-bit stereo | 44.1, 48, 88.2, 96, 176.4, 192 |
| Packed 24-bit stereo | 44.1, 48, 88.2, 96 |

For each combination, play audio for five minutes. Check left/right channels,
host volume/mute, and absence of clicks/dropouts. Capture status and diagnostics
after playback starts and again before it stops; underruns must not increase.
Record any format the OS does not expose instead of substituting a different
format. Status should show version 5.1, the selected rate/depth, streaming true,
and 220000000 Hz for PCM16/192 kHz, approximately 180 MHz for other
streaming formats, and approximately 150 MHz after returning idle.

From this directory, use `python` on Windows or `python3` on macOS:

```sh
python tools/hid_probe.py status
python tools/hid_probe.py diagnostics
python tools/hid_probe.py restart
# Wait at least three seconds for re-enumeration, then:
python tools/hid_probe.py status
```

Also exercise stop/start, changing format after idle, and disconnect/reconnect.
Report the OS/version, format/rate, before/after underruns, audible result, and
whether restart/reconnect recovered. Also exercise the existing web controller and Android controller on their
respective hosts. Host unit tests do not validate physical USB access.

For loaded playback checks, enable ten EQ bands, crossfeed, output processing,
meters and spectrum in the controller, and make repeated live edits. Use nonzero
audio with host mute disabled; silence does not exercise the complete spectrum
workload. Record status and diagnostics before and after the run, along with the
actual duration and any audible interruption. A short run or early stop does not
count as a completed long-duration test.

The retained HID utility reads status and test-build diagnostics and can request
restart or BOOTSEL. Specialized migration probe scripts are no longer shipped.

## Flash and recover

Keep a known-working program UF2 and a backup of the device's flash before
testing. Hardware acceptance remains incomplete.

```sh
python3 tools/hid_probe.py bootsel
picotool save -a -v artifacts/device-before-rust.bin
picotool load -v -x artifacts/pumper-rust-5.1.0.uf2
```

To restore the C firmware, request BOOTSEL and load the saved C **program** UF2.
Loading either program must leave the final settings region intact. A full-flash
backup includes settings; do not restore it merely to switch firmware versions.
Hardware BOOTSEL remains available using the board button. If the board stops
responding and requires that button, stop automated testing until physical
access is available.

## Compatibility fixtures

`reference/golden/` contains frozen PCM/protocol/feedback, DSP, spectrum, and
schema-5 flash outputs captured from the previous C implementation before its
removal. The manifest records source hashes, capture commands, and fixture hashes.
Rust tests retain the input corpus and original numerical tolerances. Existing
USB descriptor fixtures remain byte-exact compatibility inputs.

No C implementation is compiled to run the tests. Passing these tests
does not establish physical USB, electrical, or long-duration playback validation.
